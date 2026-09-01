# RailBlock implementation guide

This guide explains the code that exists today. It is intended for onboarding, debugging, demonstrations, and viva preparation. Paths and symbols refer to the repository root.

## 1. System mental model

RailBlock receives a fixed 28-day monthly workload and compares three ways to place every task exactly once. All three algorithms see the same preprocessed legal windows, produce placements, and then pass through the same block builder, validator, KPI calculator, evidence-trace builder, and runtime recorder.

```text
stored scenario CSV
    ↓
shared candidate windows
    ↓
Independent | Greedy | CP-SAT
    ↓
placements
    ↓
derive blocks → validate → calculate metrics/objective → task traces
    ↓
JSON → Go API → optional PostgreSQL + Next.js
```

The horizon constants in `optimizer/include/model.hpp` are:

```cpp
kSlotMinutes = 15
kSlotsPerDay = 96
kHorizonDays = 28
kHorizonWeeks = 4
kHorizonSlots = 2688
```

## 2. Repository map

```text
backend/
  cmd/api/main.go                 API composition and environment
  internal/httpapi/server.go      routes, dataset catalog, CSV responses
  internal/optimizer/runner.go    Go → C++ process/JSON boundary
  internal/store/postgres.go      benchmark JSON insert
config/
  optimizer.conf                 active weighted-objective values
  priority.conf                  explanatory only; not loaded
  train-weights.conf             explanatory only; not loaded
data/
  scenarios/scenario-{alpha,beta,gamma}/  live deterministic CSV datasets
  benchmarks/{100,250,500}/       optional offline presets
  demo/                           CLI default/legacy compatible dataset
db/migrations/                    PostgreSQL schema
frontend/app/
  page.tsx                        all pages, types, API state, drawers
  globals.css                     full responsive styling
  layout.tsx                      root metadata/layout
optimizer/
  include/model.hpp               common C++ types/constants
  include/engine.hpp              public engine functions
  src/engine.cpp                  preprocessing, algorithms, validation, output
  src/main.cpp                    CLI dispatch
  tests/engine_test.cpp           focused optimizer tests
scripts/
  setup-ortools.sh                pinned native dependency install
  dev.sh                          local API + web orchestration
  benchmark.sh                    Alpha JSON/CSV benchmark
tools/
  generate_demo.py                deterministic scenario generator
  benchmark_report.py             JSON → CSV
  verify_native_cp_sat.py         native/validator assertion
```

## 3. Optimizer in depth

### 3.1 Common types

`optimizer/include/model.hpp` is the data contract shared by every C++ path.

Inputs:

- `Corridor {id, name}`;
- `Task {id, corridor_id, department, type, duration_slots, severity, criticality, due_slot, mandatory, requires_power_block, earliest_slot, latest_end_slot}`;
- `TrainMovement {id, corridor_id, start_slot, end_slot, hard_conflict, impact_weight}`;
- `AvailabilityWindow`, `Dependency`, and the compatibility map keyed by sorted task-type pairs.

Intermediate/output types:

- `CandidateWindow` and `CandidateWindows`;
- `Placement {task_id, start_slot, end_slot}`;
- `Block {corridor_id, start_slot, end_slot}`;
- `CandidateEvaluation` and `TaskTrace`;
- `Weights`, `Metrics`, `ValidationResult`, and `Plan`.

`Plan` holds algorithm/solver identity, three runtime fields, `native_cp_sat`, placements, derived blocks, traces, metrics, and validation.

### 3.2 CSV and weight loading

`load_dataset` in `optimizer/src/engine.cpp` reads six named CSV files. The parser skips the header, empty lines, and `#` comments and reports file/row errors. `load_weights` reads integer `key=value` pairs for `wB`, `wD`, `wT`, `wL`, and `wV`.

Booleans accept `1`, `true`, `TRUE`, or `HARD`; train loading explicitly compares `conflict_mode` to `HARD`.

### 3.3 Critical work and legal latest end

`critical_task(task)` is true when any of these hold:

- `mandatory == true`;
- `severity >= 9`;
- `criticality >= 9`.

`effective_latest_end(task)` starts with `min(latest_end_slot, 2688)`. For critical tasks with a nonnegative due slot, it also takes `min(..., due_slot)`. Therefore critical due completion is a hard domain rule. For noncritical work, lateness can remain feasible and is penalized.

### 3.4 Shared candidate-window engine

`generate_candidate_windows(const Dataset&)` runs separately for each algorithm, and that duration becomes `preprocessing_ms`.

For each task it:

1. clips `earliest_slot` to zero and uses `effective_latest_end`;
2. intersects that task interval with every availability row for its corridor;
3. intersects corridor `HARD` trains with each available interval;
4. sorts and merges overlapping/touching forbidden intervals;
5. subtracts them from availability;
6. retains only free intervals at least `duration_slots` long;
7. sorts and merges touching free windows.

`SOFT` trains are intentionally not subtracted. They stay feasible and influence cost later.

`candidate_starts` samples each free window in increments of two slots (30 minutes), then appends the latest legal start if it was not already sampled. This reduces the model/search size, so the optimizer is not considering every 15-minute start even though duration and reporting use 15-minute slots.

### 3.5 Compatibility and dependencies

`compatible` sorts the two task-type names and looks up `Dataset.compatibility`. Missing pairs default to compatible.

`can_place` checks candidate membership; incompatible overlaps on the same corridor when coordination is enabled; and predecessor/successor minimum lags against already placed tasks.

`ordered_tasks` ranks by `priority_score` and performs a small topological repair so a predecessor appears before its successor. The formula is:

```text
(critical ? 10000 : 0)
+ 40*severity
+ 25*criticality
+ max(0, 2688-due_slot)/96 when due_slot is inside the month
```

This formula is hard-coded. `config/priority.conf` is not read.

### 3.6 Independent baseline

`solve_independent` measures preprocessing, then calls `departmental_schedule`.

`departmental_schedule` iterates department order `ENGINEERING`, `ST`, `TRD`, schedules each department's priority-ordered pending tasks, and repeats until all tasks are placed or no progress occurs. It uses `greedy_schedule(... coordinate=true, optimize_cost=false)`, so it takes the first feasible candidate and does not optimize the weighted objective.

This is a departmental baseline, not three completely isolated invalid schedules: combined placements still enforce incompatibility and dependency checks. That detail matters when explaining why its output passes the same validator.

### 3.7 Coordinated Greedy

`solve_greedy` uses one global priority order and calls `greedy_schedule(... coordinate=true, optimize_cost=true)`.

For every feasible candidate start, it estimates incremental:

- new block count (`block_delta`);
- new active corridor slots/downtime;
- newly impacted SOFT train weights;
- lateness minutes;
- one deadline-violation penalty.

It chooses the smallest weighted increment. If the first coordinated pass does not schedule all tasks, the implementation replaces it with `departmental_schedule` so the final validator can still enforce completeness.

### 3.8 Native OR-Tools CP-SAT

`solve_cp_sat` under `SIH_WITH_ORTOOLS` first builds the same candidates and obtains a greedy/depart­mental incumbent for hints.

Model structure:

- one Boolean `x[i][k]` per task/candidate start;
- `sum(x[i]) == 1` for every task, making scheduling compulsory;
- an integer `start_vars[i]` equal to the selected start;
- corridor/slot `block` variables representing any active maintenance;
- corridor/slot `block_start` variables for transitions from inactive to active;
- pairwise disjunctions for incompatible same-corridor task types;
- precedence constraints using predecessor duration and `min_lag_slots`;
- a SOFT-train `impacted` Boolean linked to active block slots;
- lateness integer and violation Boolean for noncritical due dates still in the horizon.

Availability, task windows, critical due dates, and HARD trains are already encoded in candidate domains, so the CP-SAT model does not need separate interval constraints for them.

Solver parameters are set directly in `solve_cp_sat`:

```text
max_time_in_seconds = CLI/API time limit
num_search_workers = 8
random_seed = 26027
```

The result maps OR-Tools status to `OPTIMAL`, `FEASIBLE`, `INFEASIBLE`, or `UNKNOWN`. `native_cp_sat` is true in this branch.

When compiled without OR-Tools, the same function name runs a one-pass local improvement over Greedy, returns `FALLBACK_FEASIBLE`, and sets `native_cp_sat=false`. This is a compatibility fallback, not CP-SAT.

### 3.9 Weighted objective

The exact implemented metric is:

```text
objective = wB * block_count
          + wD * downtime_minutes
          + wT * train_impact
          + wL * lateness_minutes
          + wV * deadline_violations
```

Committed values in `config/optimizer.conf`:

| Symbol | Value | Term |
|---|---:|---|
| `wB` | 400 | derived block count |
| `wD` | 2 | corridor downtime minute |
| `wT` | 25 | sum of impacted SOFT movement weights |
| `wL` | 5 | late minute |
| `wV` | 5000 | task finishing after due slot |

This is one weighted integer objective, not lexicographic optimization. An unscheduled penalty cannot appear because exactly-once scheduling is a hard requirement.

### 3.10 Blocks, metrics, traces, and finalization

`derive_blocks` marks each corridor slot active if at least one placement covers it and compresses contiguous active slots. Compatible simultaneous or adjacent work therefore becomes one block. Downtime is the union of active corridor minutes, not the sum of task durations.

`calculate_metrics` reports:

- block count and downtime;
- train impact: each SOFT movement weight counted once if any block overlaps it;
- scheduled/total tasks;
- critical completed/total;
- lateness minutes and deadline violations;
- weighted objective.

`build_task_traces` provides a compact explanation sample, not the solver's complete internal search log. It includes the selected candidate; up to a few feasible window endpoints; sample HARD conflicts; and a too-short availability interval if available. For selected/feasible items it computes raw SOFT `train_cost`, added downtime minutes against other blocks, and `block_reuse`.

`finalize` is shared by all algorithms. It derives blocks/traces, calls the validator and metric calculator, and sets runtimes.

Runtime meanings:

- `preprocessing_ms`: candidate-window generation only;
- `algorithm_ms`: algorithm/solver section only;
- `total_runtime_ms`: from solver entry through block derivation, traces, validation, metrics, and final reporting work;
- raw `runtime_ms`: JSON alias equal to total runtime.

## 4. Validator

There is no separate `validator/` directory. The independent validator is the public `validate(const Dataset&, const vector<Placement>&)` function declared in `engine.hpp` and implemented in `engine.cpp`.

It stays algorithm-independent because it accepts only source data and final placements. It does not inspect Greedy decisions, CP-SAT variables, objective values, or solver status.

Checks performed:

1. duplicate task IDs are rejected;
2. each placement task ID is resolved against the dataset (`task_by_id` throws and aborts finalization for an unknown ID rather than recording a normal violation);
3. duration must equal the task duration;
4. start/end must stay inside the task/effective latest window;
5. continuous availability coverage is recomputed by `independently_hard_feasible`;
6. HARD train overlaps are rejected (also reported specifically);
7. every dataset task must be present;
8. incompatible task types cannot overlap on one corridor;
9. each scheduled successor requires a scheduled predecessor;
10. predecessor end + minimum lag must not exceed successor start.

Critical due dates are enforced through `effective_latest_end`. The 28-day end is part of that effective limit. Correct corridor is implicit because placements contain only a task ID and the task's corridor is read from the dataset; placements cannot name a different corridor.

The frontend lists a “Power-block restrictions” verification category, but the current C++ validator emits no power-specific check. `requires_power_block` is not referenced by scheduling or validation logic after CSV load.

## 5. Data schemas

### 5.1 CSV runtime schema

Slots are half-open intervals `[start_slot, end_slot)`.

`tasks.csv`:

| Field | Meaning |
|---|---|
| `id` | stable task ID |
| `corridor_id` | task's fixed corridor |
| `department` | `ENGINEERING`, `ST`, or `TRD` |
| `task_type` | compatibility/presentation category |
| `duration_slots` | continuous 15-minute slots |
| `severity`, `criticality` | 1–10 scoring inputs |
| `due_slot` | completion target |
| `mandatory` | contributes to critical hard due behavior |
| `requires_power_block` | currently metadata |
| `earliest_slot`, `latest_end_slot` | legal monthly task interval |

`trains.csv` has `id,corridor_id,start_slot,end_slot,conflict_mode,impact_weight`. Every movement is electric by domain assumption, so no traction column exists.

`availability.csv`, `dependencies.csv`, and `compatibility.csv` supply legal corridor windows, directed precedence/minimum lag, and pairwise type compatibility. Unlisted compatibility pairs default true.

### 5.2 PostgreSQL schema

`db/migrations/001_init.sql` creates:

- enums `department`, `train_category`, `conflict_mode`, `algorithm`;
- `corridors(id, name)`;
- `assets(id, corridor_id, department, asset_type, asset_code, criticality)`;
- `maintenance_tasks(id, asset_id, corridor_id, department, source_type, task_type, duration_minutes, severity, criticality, reported_at, due_at, mandatory, requires_power_block, status)`;
- `train_movements(id, train_number, corridor_id, category, start_time, end_time, conflict_mode, impact_weight)` with no traction field;
- `corridor_availability(id, corridor_id, start_time, end_time)`;
- `task_dependencies(predecessor_task_id, successor_task_id, min_lag_minutes)`;
- `plans(id, dataset_id, horizon_start/end, algorithm, solver_status, preprocessing_ms, algorithm_ms, total_runtime_ms, objective_value, metrics, validation, weights, created_at)`;
- `blocks(id, plan_id, corridor_id, start_time, end_time)`;
- `block_tasks(block_id, task_id)`;
- `benchmark_runs(id, dataset_id, result, created_at)`.

`002_dataset_ids.sql` safely adds/backfills/non-nulls `dataset_id` for databases created before that field existed. Fresh initialization runs both files; `ADD COLUMN IF NOT EXISTS` makes the overlap safe.

The schema is broader than runtime persistence. Current `Store.SaveBenchmark` writes only `benchmark_runs`; there is no CSV-to-database seed loader and no code populating plans/blocks/tasks.

## 6. Demo generator

`tools/generate_demo.py` uses only Python's standard library and `random.Random(seed)`. `make generate` supplies fixed profiles and seeds:

| Scenario | Seed | Tasks | Trains/day | Dept distribution | Power tasks | Dependencies |
|---|---:|---:|---:|---|---:|---:|
| Alpha | 26027 | 110 | 105 | 60 Eng / 30 S&T / 20 TRD | 13 | 9 |
| Beta | 26127 | 124 | 130 | 20 Eng / 54 S&T / 50 TRD | 27 | 12 |
| Gamma | 26227 | 120 | 115 | 40 each | 30 | 18 |

All use the same 10 named corridors. Tasks are distributed round-robin by corridor. Profiles change department cycles, power-work ratio (12%, 22%, 25%), and dependency target (8%, 10%, 15%).

Task durations are chosen from 2, 4, 6, 8, or 12 slots. Each round is assigned to one of four weeks, an earliest day, and a 5–7 day legal window. Roughly 10% are critical and another 10% high-priority; the first eight tasks are mandatory when the dataset is large enough.

Every corridor receives three availability windows per day: slots 0–24, 40–68, and 82–96 relative to that day.

Train generation creates the configured count each day, distributes movements across corridors, and marks one in five pattern positions `HARD`; all others are `SOFT`. HARD weights are 10; SOFT weights are sampled from 1, 3, 5, 7. Movement duration is one or two slots.

Dependencies link same-corridor tasks in adjacent rounds with 0–2 slot lag and avoid crossing each four-week-cycle boundary. Compatibility always writes four pairs, including three incompatible pairs and one explicit compatible pair.

`make generate` overwrites committed scenario CSVs. Use `git diff -- data/scenarios` to confirm deterministic reproduction before committing generator changes.

## 7. Backend architecture and API

### Composition

`backend/cmd/api/main.go` reads:

- `DATABASE_URL` (blank disables persistence);
- `API_ADDR` (default `:8080`);
- `OPTIMIZER_BIN`;
- `DATA_ROOT`;
- `OPTIMIZER_CONFIG`;
- `SOLVER_TIME_LIMIT_SECONDS` (default 15).

It opens pgx when configured, but logs and continues if PostgreSQL is unavailable.

### Dataset selection

`datasetDefinitions` in `backend/internal/httpapi/server.go` is the allowlist. IDs are mapped to `filepath.Join(DATA_ROOT, id)` only after an exact match, preventing arbitrary directory selection. Alpha is the default.

For `GET`, `dataset_id` comes from the query. For benchmark `POST`, it comes from JSON body `{ "dataset_id": "..." }`. An empty value defaults to Alpha. Invalid JSON or unknown IDs return 400.

### Routes and response schemas

`GET /api/health`:

```json
{"status":"ok","slot_minutes":15,"horizon_days":28,"horizon_weeks":4}
```

`GET /api/datasets` returns `default_dataset_id` and items with `id`, `label`, `description`, `default`, `task_count`, `corridor_count`, and `train_movement_count`.

`GET /api/dataset` returns `dataset_id`, label, `all_trains_electric:true`, horizon fields, and arrays `corridors`, `tasks`, `trains`, `availability`, `dependencies`, `compatibility`. CSV values are strings because `readCSV` returns `map[string]string`.

`GET/POST /api/benchmark` returns:

```text
dataset_id, horizon_days, horizon_weeks, horizon_slots, slot_minutes, plans[]
```

Every plan has `dataset_id`, `algorithm`, `solver_status`, runtime fields, `native_cp_sat`, `validation`, `metrics`, `placements`, `blocks`, and `task_traces` (the runner's Go struct does not declare traces, but its generic JSON mutation preserves them).

`GET /api/plans/{algorithm}` accepts only `independent`, `greedy`, or `cp-sat`.

### Process orchestration

`CommandRunner.run` uses `exec.CommandContext` with arguments `command --data ... --config ... --time-limit ...`. Process timeout is `TimeLimit + 5s`. It uses combined stdout/stderr, rejects nonzero exits, unmarshals JSON into a generic map, injects dataset IDs, and marshals it again.

Benchmark runs all algorithms sequentially inside one C++ process in order: Independent, Greedy, CP-SAT. If persistence is enabled, Go stores the returned document after the optimizer succeeds. Persistence failure is logged but does not fail the HTTP response.

## 8. Frontend architecture

The frontend is Next.js App Router with React 19. `frontend/app/page.tsx` is a single client component containing all prototype views and local state.

`Home` manages selected page, algorithm, dataset, benchmark, loading/error state, and active drawer. Initial load fetches the catalog, selects the server default, then `run` fetches selected benchmark and raw dataset concurrently. It checks outer/nested `dataset_id` consistency before rendering.

Pages:

- `Overview`: treats CP-SAT as recommended, compares it with Independent, and shows weekly distribution.
- `Planner`: a seven-day slice of the 28-day plan with algorithm, week, corridor, and department filters; separate train, block, Engineering, S&T, and TRD lanes.
- `Tasks`: monthly task table with search and status/priority/department/week filters.
- `Verification`: solver status, validator result, violations, and runtime composition. Its category counts are UI string classification, not separate validator executions.
- `BenchmarkPage`: normalized operational-cost chart, runtime chart, and full metric table for all three algorithms.

`TaskDrawer` shows task metadata, selected placement/block, compact candidate trace, and evidence-derived reasons. `BlockDrawer` shows tasks grouped by department, client-derived checks, and a simple consolidation benefit (`tasks in block - 1`). Drawer checks explain existing results; the authoritative validation remains C++.

## 9. End-to-end flows

### Scenario selection and benchmark

```text
user selects Scenario Beta
  → Home.run("scenario-beta")
  → POST /api/benchmark {dataset_id}
      + GET /api/dataset?dataset_id=... in parallel
  → Go resolves data/scenarios/scenario-beta
  → C++ loads same CSV/config and runs all three algorithms
  → each finalizes through shared blocks/validator/metrics/traces
  → Go injects dataset IDs
  → optional benchmark_runs insert
  → frontend verifies IDs and updates every page
```

### One-plan API

```text
GET /api/plans/cp-sat?dataset_id=scenario-gamma
  → route and algorithm allowlist
  → C++ cp-sat command for Gamma
  → finalized one-plan JSON
```

The current UI does not call the one-plan endpoint; it always uses benchmark so all comparison pages share one live run.

### Persistence

```text
successful benchmark JSON
  → Store.SaveBenchmark
  → benchmark_runs(dataset_id, result)
```

There is no read-history endpoint and no normalized plan write path.

## 10. Configuration and tuning

| What to tune | Exact location/symbol |
|---|---|
| objective coefficients | `config/optimizer.conf`; `Weights`; `load_weights` |
| default CLI limit | `option(... "--time-limit", "15")` in `main.cpp` |
| API limit | `SOLVER_TIME_LIMIT_SECONDS`, read by `envSeconds` in Go `main.go` |
| benchmark limit | `SOLVER_TIME_LIMIT_SECONDS` in `scripts/benchmark.sh` |
| CP-SAT workers/seed | `parameters.set_num_search_workers(8)` / `set_random_seed(26027)` |
| candidate granularity | `start += 2` in `candidate_starts` |
| heuristic order | `priority_score` / `ordered_tasks` |
| scenario sizes/seeds | `generate` recipes in `Makefile` |
| generator distributions | constants/profile maps in `generate_demo.py` |
| API catalog/default | `datasetDefinitions` / `defaultDatasetID` in `server.go` |

Changing objective weights needs no rebuild, because the CLI reloads the config each invocation. Changing C++ parameters or candidate granularity needs a rebuild. Changing `NEXT_PUBLIC_API_URL` needs a frontend restart in development and rebuild for production.

Do not assume `priority.conf` or `train-weights.conf` changes runtime behavior until loaders are implemented.

## 11. Run, seed, benchmark, test, troubleshoot

```bash
cp .env.example .env
make setup             # dependencies + native verification
make generate          # deterministic Alpha/Beta/Gamma
make dev               # native local API + Next.js
make test              # CTest + Go + TypeScript
make benchmark         # Alpha JSON/CSV artifacts
make db-up              # optional PostgreSQL
```

For failures:

- `native_cp_sat:false`: likely Docker or `build-portable`; use `build/optimizer/sih-optimizer` and `make verify-native`.
- optimizer missing: run `make build-optimizer` and check `OPTIMIZER_BIN` relative to the backend process.
- unknown dataset: use one of the three allowlisted IDs and confirm `DATA_ROOT` contains it.
- dataset mismatch in UI: verify benchmark and dataset calls reach the same API and no stale proxy/cache changes response IDs.
- PostgreSQL tables absent: apply `001_init.sql` then `002_dataset_ids.sql`, or initialize a fresh Compose volume.
- changed database task has no effect: expected; optimizer input is CSV.
- changed `priority.conf`/`train-weights.conf` has no effect: expected; they are not runtime-loaded.
- CP-SAT `UNKNOWN`: raise the time limit or reduce scenario/model size; still rely on validator for any returned placements.

## 12. Demo/viva mental model

The concise technically accurate explanation is:

> RailBlock is a monthly coordination optimizer, not a weekly greedy calendar. We convert 28 days into 2,688 fifteen-minute slots and preprocess the same legal candidate windows for all algorithms. HARD trains are removed from the domain; SOFT electric movements remain feasible and are penalized. Independent provides a departmental first-feasible baseline, Greedy minimizes incremental weighted cost, and OR-Tools CP-SAT optimizes the complete month with exactly-one task placement. We then ignore the solver's claim and validate the returned placements independently, derive consolidated corridor blocks, calculate the same weighted KPIs and runtimes, and compare all three on the exact same selected deterministic scenario.

Likely viva follow-ups:

- **Why weighted rather than lexicographic?** The current implementation exposes explicit trade-offs in one configurable integer objective and reports every component separately.
- **Why can SOFT trains overlap?** They model costly but feasible operating impact; HARD movements model protected intervals.
- **How is every task guaranteed?** CP-SAT has `sum(candidate starts)==1`; heuristics are rejected by the validator if any monthly task is missing.
- **Why is downtime less than task-duration sum?** Blocks are the union of active corridor slots, so compatible simultaneous/adjacent work shares infrastructure downtime.
- **Is the validator really independent?** It consumes only dataset plus placements and recomputes rules; it does not inspect solver variables or status.
- **Does PostgreSQL drive the plan?** Not yet. CSV is authoritative input; PostgreSQL currently keeps benchmark JSON only.
- **Is power isolation separately modeled?** No. Power tasks are flagged and shown, but current hard behavior is the shared all-electric HARD/SOFT and other common constraints.
- **Are trace candidates exhaustive?** No. They are a compact evidence sample for explanation; CP-SAT/Greedy may evaluate many more starts.
