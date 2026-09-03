# Solver and optimization deep dive

This document describes the implemented C++ scheduling engine in `optimizer/`. It should be read with [`DATA_SCHEMA_AND_PREPROCESSING.md`](DATA_SCHEMA_AND_PREPROCESSING.md) and [`ML_PRIORITY_MODEL.md`](ML_PRIORITY_MODEL.md).

## 1. Optimization contract

The solver receives:

- one 28-day dataset;
- one ML-generated priority score in `[0,100]` for every task;
- five operational objective weights plus one priority-delay weight;
- a CP-SAT time limit.

It must place every monthly task exactly once. It does not choose which work to omit. A missing placement is invalid, even if an algorithm otherwise reports a feasible-looking status.

Time is represented as half-open intervals `[start_slot,end_slot)` on a 2,688-slot grid:

```text
15 minutes/slot × 96 slots/day × 28 days = 2,688 slots
```

The public engine contract is declared in `optimizer/include/engine.hpp`; shared types and constants are in `optimizer/include/model.hpp`; implementation is in `optimizer/src/engine.cpp`; CLI parsing is in `optimizer/src/main.cpp`.

## 2. Inputs required by the CLI

```text
sih-optimizer <independent|greedy|cp-sat|benchmark>
  --data <scenario-directory>
  --priorities <task-priority-csv>
  --config <optimizer.conf>
  --time-limit <seconds>
```

`--priorities` is mandatory. `main.cpp` loads the scenario with `load_dataset`, then calls `load_priorities`, then `load_weights`. C++ does not load the joblib model and cannot derive a fallback priority formula.

`load_priorities` requires exactly one row per dataset task:

```csv
task_id,priority_score
T001,71.234567
```

Scores must be finite and between 0 and 100. Missing, duplicate, unknown, or extra task IDs abort the CLI. This fail-closed behavior prevents the three algorithms from silently using different priority data.

## 3. Shared candidate preprocessing

`generate_candidate_windows(const Dataset&)` creates legal free intervals independently of any scheduling algorithm.

### 3.1 Effective task window

For each task:

```text
task_start = max(0, earliest_slot)
task_end   = min(latest_end_slot, 2688)
```

`critical_task` returns true when `mandatory` is true, severity is at least 9, or criticality is at least 9. If a critical task has a nonnegative `due_slot`, `effective_latest_end` also clips `task_end` to that due slot. Thus critical due dates are hard domain limits; noncritical lateness can remain feasible and is priced.

### 3.2 Availability intersection

Only availability rows for the task's corridor are considered. Each row is intersected with the effective task window. An intersection shorter than `duration_slots` is discarded immediately.

### 3.3 HARD train subtraction

For each surviving availability interval, preprocessing collects only overlapping `HARD` movements on the same corridor. It clips them to the available interval, sorts them, and merges overlapping or touching forbidden intervals.

The merged HARD intervals are subtracted from availability. Free pieces shorter than the task duration are removed. Free pieces from all availability rows are then sorted and merged when touching or overlapping.

`SOFT` train movements are not removed. They remain legal and later contribute train-impact cost.

### 3.4 Candidate starts

`candidate_starts` turns each free interval into discrete starts:

```cpp
start += 2
```

This means normal candidate starts are 30 minutes apart even though the underlying slot is 15 minutes. The latest legal start in each window is appended when it is not already present, so the window's right boundary remains representable.

If a task has no candidate starts, CP-SAT receives an exactly-one equation over an empty sum and becomes infeasible. Heuristics leave the task unscheduled, which the validator rejects.

## 4. Shared feasibility helpers

`compatible(data,a,b)` looks up a sorted task-type pair. Missing pairs default to compatible.

`can_place` checks:

- the proposed start belongs to the task's candidate windows;
- overlapping same-corridor work is compatible when coordination is enabled;
- already placed predecessors finish plus minimum lag before the task;
- already placed successors begin after the task plus minimum lag.

`ordered_tasks` sorts by descending `Task.priority_score`, then performs a small topological repair: when a predecessor appears after its successor, `std::rotate` moves it ahead. This is not a complete general-purpose topological sort, but generated dependencies are simple forward links.

## 5. Independent baseline

Entry point: `solve_independent`.

Flow:

1. generate and time common candidate windows;
2. call `departmental_schedule`;
3. iterate departments in fixed order: `ENGINEERING`, `ST`, `TRD`;
4. sort each department's pending work by ML priority;
5. use `greedy_schedule` with `coordinate=true`, `optimize_cost=false`;
6. take the first feasible candidate for each task;
7. repeat passes until all tasks are placed or no progress is possible.

The baseline is “independent” in decision style, not in safety enforcement. Its combined schedule still checks cross-department incompatibilities and dependencies, which is why it can be compared through the same validator.

The Independent algorithm uses ML scores for ordering. It does not search candidate starts using the weighted objective and therefore does not explicitly minimize `wP` or other objective terms during placement.

## 6. Coordinated Greedy

Entry point: `solve_greedy`.

It processes one global descending-ML-priority task order. For each legal candidate start, it estimates the incremental weighted cost against placements already chosen.

### 6.1 Block delta

`derive_blocks` is called for the partial schedule. The candidate counts previously uncovered corridor slots and blocks it touches. Its approximate block change is:

```text
block_delta = 0                         when every candidate slot is covered
block_delta = 1 - touching_block_count  otherwise
```

This rewards reuse and joining adjacent/touching blocks.

### 6.2 Candidate value

The Greedy candidate value includes:

```text
wB × block_delta
+ wD × new_active_slots × 15
+ wT × newly_impacted_soft_train_weights
+ wL × late_minutes
+ wV when the due date is violated
+ wP × rounded_priority × delay_days
```

`delay_days` is `ceil(max(0,start_slot-earliest_slot)/96)`. Starting even one slot after an exact day boundary counts as the next day of delay. The ML score is rounded with `std::llround` before multiplying.

A SOFT movement is charged only if it overlaps the candidate and is not already impacted by an existing block. The lowest-cost feasible candidate is selected. Ties keep the first encountered candidate.

If the coordinated pass is incomplete, `solve_greedy` discards it and replaces it with `departmental_schedule`; validation still decides whether the result is acceptable.

## 7. Native OR-Tools CP-SAT

Entry point: the `SIH_WITH_ORTOOLS` branch of `solve_cp_sat`.

### 7.1 Warm-start incumbent

Before building the model, CP-SAT runs coordinated Greedy; if incomplete, it runs the departmental baseline. Selected starts from this incumbent become Boolean hints. Hints guide search but do not constrain the answer.

### 7.2 Decision variables

For task `i` and candidate start `k`:

- `x[i][k]`: Boolean, true when that start is selected;
- `start_vars[i]`: integer selected start;
- `block[corridor,t]`: Boolean corridor activity at slot `t`;
- `block_start[corridor,t]`: Boolean transition into an active block;
- one `impacted` Boolean per SOFT train;
- one lateness integer and violation Boolean for each applicable due date.

### 7.3 Exactly-once scheduling

For every task:

```text
sum_k x[i][k] = 1
start_vars[i] = sum_k candidate_start[i][k] × x[i][k]
```

There is no optional interval and no unscheduled decision variable.

### 7.4 Corridor activity and blocks

Every selected task/start implies activity for each covered corridor slot. The model constrains `block[c,t]` to the logical OR of those active task-start variables. `block_start[c,t]` is true exactly when `block[c,t]` is true and the previous slot is false; at slot zero it equals activity.

Summing `block_start` gives the number of consolidated blocks. Summing `block` and multiplying by 15 gives corridor downtime minutes.

### 7.5 Incompatible work

For each incompatible task pair on the same corridor, CP-SAT creates one ordering Boolean. One enforced inequality puts task A before B; the negated branch puts B before A. Compatible tasks may overlap and share a block.

### 7.6 Dependencies

For each dependency:

```text
start(predecessor) + duration(predecessor) + min_lag
    <= start(successor)
```

### 7.7 Train impact

HARD trains are absent from the candidate domain, so no selected start can cross them. Each SOFT train's `impacted` variable is equivalent to “any corridor block slot overlaps this movement.” Its `impact_weight` is counted once, regardless of overlap length or number of tasks.

### 7.8 Lateness and priority delay

For noncritical due dates still represented as soft targets:

```text
lateness_slots = max(end_slot - due_slot, 0)
violation      = (lateness_slots >= 1)
```

The objective converts lateness slots to minutes. Priority delay is precomputed for each candidate as rounded ML score times delay days and added through `x[i][k]`.

### 7.9 Solver settings and status

`solve_cp_sat` configures:

```text
max_time_in_seconds = --time-limit
num_search_workers  = 8
random_seed         = 26027
```

OR-Tools status maps to `OPTIMAL`, `FEASIBLE`, `INFEASIBLE`, or `UNKNOWN`. Placements are extracted only for OPTIMAL or FEASIBLE responses. `native_cp_sat=true` identifies this branch.

## 8. Portable fallback

When built with `SIH_WITH_ORTOOLS=OFF`, `solve_cp_sat` is a Greedy-plus-local-improvement fallback. It tries moving one placement at a time to the first candidate that improves the full calculated objective. It reports `FALLBACK_FEASIBLE` and `native_cp_sat=false`.

The API Dockerfile deliberately builds this branch. It is useful for application portability but must not be described as OR-Tools or CP-SAT in results.

## 9. Exact weighted objective

`config/optimizer.conf` currently contains:

```text
wB=400
wD=2
wT=100
wL=5
wV=5000
wP=1
```

The final objective is:

```text
400 × block_count
+   2 × downtime_minutes
+ 100 × train_impact
+   5 × lateness_minutes
+ 5000 × deadline_violations
+   1 × priority_weighted_delay
```

Where:

```text
train_impact = sum impact_weight once per overlapping SOFT movement

priority_weighted_delay = sum over tasks of
  round(ML priority score) × ceil(max(0,start-earliest)/96)
```

This is a weighted sum, not lexicographic optimization. Weight scales matter: changing `wT`, for example, changes the exchange rate between one unit of train impact, block count, downtime, lateness, and priority delay.

## 10. Result extraction and common finalization

Every algorithm calls `finalize`, which:

1. stores placements and algorithm identity;
2. calls `derive_blocks`;
3. calls `build_task_traces`;
4. calls the independent `validate` function;
5. calls `calculate_metrics` with the same weights;
6. records runtime fields.

`derive_blocks` computes the union of active task slots separately per corridor and compresses contiguous active slots. Therefore downtime is corridor-union time, not summed task duration.

`build_task_traces` is an explanation sample, not a complete search trace. It includes the selected interval, several feasible endpoints, sample HARD conflicts, and a too-short interval when available. It derives SOFT train cost, added downtime, and block reuse against the other selected placements.

## 11. Runtime measurement

Each `solve_*` function uses `std::chrono::steady_clock`.

- `preprocessing_ms`: the first `generate_candidate_windows` call.
- `algorithm_ms`: heuristic work, or for CP-SAT the incumbent, model construction, solve, and extraction.
- `total_runtime_ms`: from entry into `solve_*` through finalization, including block/traces/validator/metrics.
- `runtime_ms`: backward-compatible JSON alias for `total_runtime_ms`.

Not included in these C++ timings:

- reading scenario CSV;
- loading the temporary priority CSV;
- loading objective configuration;
- Python model startup and inference;
- Go JSON wrapping or PostgreSQL persistence.

The Go request context covers ML inference and optimizer execution together, with a timeout of `SOLVER_TIME_LIMIT_SECONDS + 5 seconds`. Consequently, reported `total_runtime_ms` is algorithm-comparison time, not complete HTTP latency.

## 12. Independent validation

`validate(data,placements)` consumes only dataset and placements. It does not inspect solver variables, objective claims, ML scores, or solver status.

It checks duplicate/missing tasks, exact duration, task/effective due window, continuous corridor availability, HARD train exclusion, incompatible overlaps, and dependency/minimum-lag order. Unknown placement task IDs raise during lookup rather than becoming a normal violation.

ML priority is intentionally not a hard rule. A schedule can be valid even when a low-priority task is early; priority affects heuristic order and objective quality, never safety or exactly-once completion.

## 13. Complexity and practical limits

Model size is driven by tasks × candidate starts plus corridors × 2,688 block slots. Pairwise incompatibility constraints can grow quadratically with tasks on a corridor. Candidate sampling at two-slot increments is the main implemented domain reduction.

The 15-second limit bounds search, not model construction alone. A FEASIBLE result is acceptable when the validator passes; OPTIMAL means OR-Tools also proved no lower objective exists within the model.

## 14. Tests that protect solver behavior

`optimizer/tests/engine_test.cpp` covers:

- merged HARD forbidden intervals;
- SOFT movement feasibility and impact;
- clear candidate preference;
- exactly-once/unscheduled validation;
- evidence traces;
- higher ML priority receiving an earlier first-feasible placement.

`optimizer/CMakeLists.txt` also registers `optimizer_requires_ml_priorities`, a WILL_FAIL test proving the CLI refuses to run without `--priorities`. `make verify-native` runs ML inference, invokes native CP-SAT, and requires both `native_cp_sat=true` and validator PASS.
