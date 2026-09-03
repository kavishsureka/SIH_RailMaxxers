# RailBlock — SIH 26027 prototype

RailBlock plans one compulsory month of railway maintenance for Engineering, S&T, and TRD. The implemented stack is Next.js 16, Go 1.23, Python/scikit-learn, C++20 with OR-Tools CP-SAT, and optional PostgreSQL 17.

The grid is fixed at 28 days × 96 fifteen-minute slots = 2,688 slots. Every task in the selected monthly dataset must be placed exactly once. All trains are electric; there is no diesel or traction field in the CSV, API, C++, or database contracts.

## Architecture

```text
Next.js dashboard
        │ REST/JSON
        ▼
Go API ───────── optional PostgreSQL benchmark persistence
        │ one batch inference per selected scenario
        ▼
Persisted GradientBoostingRegressor
        │ same priority CSV for all algorithms
        ▼
C++ optimizer CLI
  ├─ shared candidate-window preprocessing
  ├─ Independent departmental baseline
  ├─ coordinated Greedy
  ├─ native OR-Tools CP-SAT
  └─ shared block builder, validator, metrics, and traces
```

C++ is the scheduling source of truth. Go resolves the selected scenario, asks the already-trained ML model for one batch of task scores, invokes the CLI with those scores, checks its JSON, adds model metadata and `dataset_id`, and optionally saves a benchmark document. The browser does not duplicate priority or scheduling rules. Mandatory and safety constraints remain outside ML.

The committed v1 model is a prototype policy surrogate. It is trained on deterministic synthetic tasks whose offline labels come from the isolated bootstrap policy in `ml/src/labels.py`; this is not evidence of real Indian Railways behavior. With pilot data, the model would be retrained and recalibrated on historical planner decisions and outcomes while the `Task Features -> ML Priority -> Optimizer` boundary stays stable.

## Quick start

Requirements: CMake 3.20+, a C++20 compiler, Go 1.23+, Node.js 22+, npm, Python 3, and `make`.

```bash
cp .env.example .env
make setup
make dev
```

Open [http://localhost:3000](http://localhost:3000). Leave `DATABASE_URL` blank for database-free development.

```bash
docker compose up --build
```

The API container deliberately builds the portable fallback (`native_cp_sat: false`); native OR-Tools is the normal local build and benchmark path.

## Deterministic demo scenarios

Scenario selection is request-scoped through `dataset_id`; no environment variable selects a scenario. `make generate` reproduces all three committed medium datasets using fixed seeds in the `Makefile`.

| Dataset ID | Tasks | Trains | Profile |
|---|---:|---:|---|
| `scenario-alpha` | 110 | 2,940 (105/day) | Engineering-heavy, moderate traffic; default |
| `scenario-beta` | 124 | 3,640 (130/day) | Higher traffic and S&T/TRD-heavy |
| `scenario-gamma` | 120 | 3,220 (115/day) | Balanced departments, more dependencies and power work |

Every scenario has 10 corridors and is stored under `data/scenarios/`.

## Algorithms and objective

- **Independent** schedules departments in priority passes and takes the first feasible candidate. It is the baseline, although its combined result still respects shared incompatibilities and dependencies.
- **Greedy** uses one coordinated priority order and chooses the lowest incremental weighted cost. If that pass is incomplete, it falls back to the departmental scheduler.
- **CP-SAT** solves the full month with Boolean candidate starts, exactly-one constraints, corridor activity and block-start variables, incompatibilities, dependencies, and one weighted objective. It uses eight workers, random seed `26027`, and the configured time limit.

All algorithms use `generate_candidate_windows`, `derive_blocks`, `validate`, `calculate_metrics`, and `finalize` in `optimizer/src/engine.cpp`.

Preprocessing intersects task and corridor-availability windows, clips critical work to its due slot, subtracts merged `HARD` train intervals, and removes intervals shorter than the task. Candidate starts are sampled every two slots (30 minutes), with the latest legal start also included. `SOFT` movements stay feasible and add train-impact cost when a derived block overlaps them. Power-block tasks use this same implemented contract.

The active objective in `config/optimizer.conf` is weighted, not lexicographic:

```text
400*block_count + 2*downtime_minutes + 100*train_impact
+ 5*lateness_minutes + 5000*deadline_violations
+ 1*priority_weighted_delay_score_days
```

There is no unscheduled-task term because unscheduling is invalid. `priority_weighted_delay_score_days` is rounded ML priority multiplied by whole/partial days after the task's earliest slot, so priority changes when work occurs rather than whether it occurs. Independent and Greedy order by the shared ML score; Greedy and CP-SAT also price this configured delay term. Every plan reports `preprocessing_ms`, `algorithm_ms`, and `total_runtime_ms`; raw C++ JSON also emits the compatibility alias `runtime_ms`.

## Independent validation

The validator checks returned placements rather than trusting solver status:

1. every dataset task appears exactly once;
2. placement duration equals `duration_slots`;
3. placement stays inside its task window and the month;
4. critical work (`mandatory`, severity ≥ 9, or criticality ≥ 9) finishes by its due slot;
5. the continuous interval is covered by corridor availability;
6. no task overlaps a `HARD` train;
7. incompatible task types do not overlap on one corridor;
8. dependencies and minimum lags hold.

`requires_power_block` is currently data/UI metadata. It adds no separate optimizer or validator restriction beyond those shared rules.

## Frontend

`frontend/app/page.tsx` contains five views: Overview, Block Planner, Maintenance Tasks, Plan Verification, and Benchmark. The Block Planner has week/corridor/department filters and train, block, and task lanes. Task detail shows the predicted score, source, and model version without invented per-feature explanations. Overview includes an AI Priority Model card populated from persisted metadata. Changing scenario or clicking **Run benchmark** benchmarks all three algorithms on that selected dataset and reloads its CSV view.

## REST API

| Method and path | Request/result |
|---|---|
| `GET /api/health` | service and horizon constants |
| `GET /api/datasets` | three-scenario catalog and CSV counts |
| `GET /api/dataset?dataset_id=scenario-alpha` | selected raw CSV rows and metadata; Alpha default |
| `GET /api/benchmark?dataset_id=scenario-alpha` | all algorithms on one dataset |
| `POST /api/benchmark` | JSON body `{"dataset_id":"scenario-alpha"}`; optionally persisted |
| `GET /api/plans/{independent\|greedy\|cp-sat}?dataset_id=scenario-alpha` | one finalized plan |

Unknown dataset IDs return HTTP 400. Go adds `dataset_id` to the benchmark and each nested plan. Benchmark and plan responses also expose `priority_model`, `task_priorities`, `priority_source`, and `priority_model_version`; the metadata and metrics are read from the persisted model artifact.

## Commands and documentation

```bash
make generate          # reproduce Alpha/Beta/Gamma
make train-ml          # retrain from ml/config/model.json
make test-ml           # ML labels/features/training/inference checks
make build-optimizer   # native OR-Tools build
make verify-native     # require native_cp_sat=true and validator PASS
make test              # CTest, Go tests, TypeScript check
make benchmark         # scenario-alpha to JSON + CSV
make build-portable    # separate troubleshooting fallback
```

Model hyperparameters and the random seed are in `ml/config/model.json`. Scheduling objective weights, including `wP`, are in `config/optimizer.conf`. See [`ml/README.md`](ml/README.md) for exact generation, training, inference, and evaluation commands.

- [`docs/IMPLEMENTATION_GUIDE.md`](docs/IMPLEMENTATION_GUIDE.md) — detailed modules, schemas, flows, tuning, and viva mental model.
- [`docs/SOLVER_AND_OPTIMIZATION.md`](docs/SOLVER_AND_OPTIMIZATION.md) — candidate domains, all three algorithms, CP-SAT variables/constraints, objective, extraction, validation, and runtimes.
- [`docs/DATA_SCHEMA_AND_PREPROCESSING.md`](docs/DATA_SCHEMA_AND_PREPROCESSING.md) — every CSV/API/database field and the exact preprocessing transformation.
- [`docs/ML_PRIORITY_MODEL.md`](docs/ML_PRIORITY_MODEL.md) — features, synthetic supervision, Gradient Boosting training, inference, integration, metrics, limitations, and production path.
- [`docs/REPOSITORY_GUIDE.md`](docs/REPOSITORY_GUIDE.md) — directory and file ownership.
- [`docs/SETUP_AND_RUNNING.md`](docs/SETUP_AND_RUNNING.md) — setup, database, tests, and troubleshooting.
- [`docs/NATIVE_CPSAT_TEAM_SETUP.md`](docs/NATIVE_CPSAT_TEAM_SETUP.md) and [`optimizer/ORTOOLS.md`](optimizer/ORTOOLS.md) — native solver setup.
- [`docs/design-notes.md`](docs/design-notes.md) — implemented choices and limits.

## Persistence boundary and limits

`db/migrations/001_init.sql` defines normalized corridor, asset, task, train, availability, dependency, plan, block, block-task, and benchmark tables. `002_dataset_ids.sql` backfills/guards dataset IDs for upgraded databases. The current Go store writes only `benchmark_runs(dataset_id, result)`; optimizer input remains committed CSV, and normalized plan tables are not populated.

Authentication, GIS, Kafka/Redis, resource capacity, detailed electrical-isolation topology, live railway integrations, real-time replanning, and database-backed optimizer input are outside this prototype. Synthetic weights and movements are demo assumptions, not railway standards.
