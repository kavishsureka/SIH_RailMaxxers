# Implemented prototype design notes

This file records decisions visible in the current source, not a future architecture proposal.

## Planning contract

- One 28-day month contains 2,688 fifteen-minute slots.
- Every task in the chosen monthly CSV dataset is compulsory and must be scheduled exactly once.
- Engineering, S&T, and TRD work may share a corridor block when task types are compatible.
- Every train is electric. No traction/diesel field exists.
- `HARD` movements are forbidden; `SOFT` movements remain feasible and add impact when a block overlaps them.
- `requires_power_block` is currently metadata. Power work uses the same implemented availability, HARD/SOFT, compatibility, dependency, and completion rules.

## Shared algorithm contract

`generate_candidate_windows` in `optimizer/src/engine.cpp` is run and timed for Independent, Greedy, and CP-SAT. It intersects task and availability windows, uses the due slot as a hard latest end for critical work, subtracts merged HARD intervals, and drops short windows.

All three algorithms pass through `finalize`, which derives blocks, builds compact task traces, invokes the validator, calculates common metrics/objective, and records `preprocessing_ms`, `algorithm_ms`, and `total_runtime_ms`.

The objective is a weighted sum, not lexicographic:

```text
wB*block_count + wD*downtime_minutes + wT*train_impact
+ wL*lateness_minutes + wV*deadline_violations
+ wP*priority_weighted_delay_score_days
```

`config/optimizer.conf` currently sets `wB=400`, `wD=2`, `wT=100`, `wL=5`, `wV=5000`, and `wP=1`. Unscheduled work is not a term because it violates the monthly contract. ML priority therefore affects timing through the score-day delay term, not task inclusion.

## Demo scenarios

| Scenario | Seed | Tasks | Trains/day | Emphasis |
|---|---:|---:|---:|---|
| Alpha | 26027 | 110 | 105 | Engineering-heavy default |
| Beta | 26127 | 124 | 130 | denser traffic; S&T/TRD-heavy |
| Gamma | 26227 | 120 | 115 | balanced departments; more dependencies/power work |

All have 10 corridors. The request `dataset_id` selects one directory at runtime; `.env` only supplies the base `DATA_ROOT`. The UI benchmarks all three algorithms on that same selected scenario. `make generate-presets` still creates separate 100/250/500-task offline scalability fixtures.

## Runtime and persistence

- Go runs one batch through the persisted `GradientBoostingRegressor` before invoking C++; all three algorithms consume the same temporary priority CSV.
- The v1 model is a synthetic bootstrap-policy surrogate, not evidence learned from Indian Railways history. Safety and compulsory scheduling remain non-ML constraints.
- Native OR-Tools is the default local build; the separately built fallback reports `native_cp_sat: false`.
- Go owns HTTP, dataset resolution, subprocess timeout/JSON checks, and optional persistence—not scheduling.
- PostgreSQL is optional. Current code writes only benchmark JSON even though migrations define normalized planning entities.
- The five views and task/block drawers are colocated in `frontend/app/page.tsx` for prototype speed.

## Deferred scope

Authentication, organization hierarchy, crew/equipment capacity, detailed electrical isolation, Kafka, Redis, GIS, live integrations, stochastic forecasts, real-time replanning, rolling horizons, database ingestion, browser automation, real railway ML training data, calibration, and model monitoring remain outside the implementation.
