# Data schema and preprocessing deep dive

This document describes the implemented data contracts from stored CSV through ML priorities, C++ in-memory structures, API JSON, and PostgreSQL. It also details candidate-window preprocessing.

## 1. Sources of truth

There are four related but distinct schemas:

1. `data/scenarios/*/*.csv` is the current optimizer input.
2. The ML feature extractor reads `tasks.csv` and emits a temporary priority CSV.
3. C++ structures in `optimizer/include/model.hpp` are the scheduling model.
4. `db/migrations/*.sql` defines a broader relational persistence design, but current Go code writes only `benchmark_runs`.

Do not assume that editing PostgreSQL task rows changes a plan. The optimizer reloads the selected scenario directory on every request.

## 2. Time and interval conventions

All runtime CSV/C++ intervals use integer slots and half-open bounds:

```text
[start_slot,end_slot)
```

Two intervals overlap when `a_start < b_end && b_start < a_end`. Adjacent intervals such as `[10,20)` and `[20,30)` do not overlap, although derived corridor activity compresses adjacent active slots into one continuous block.

Constants:

| Constant | Value |
|---|---:|
| Slot length | 15 minutes |
| Slots/day | 96 |
| Horizon | 28 days / 4 weeks |
| Horizon slots | 2,688 |

Slot zero is the beginning of Day 1. The frontend displays `day=floor(slot/96)+1`, week `floor(slot/672)+1`, and time from `slot % 96`.

## 3. Scenario directory contract

Every accepted dataset directory must contain exactly these named files:

```text
corridors.csv
tasks.csv
trains.csv
availability.csv
dependencies.csv
compatibility.csv
```

The C++ loader requires at least one corridor and one task. It does not currently run a separate referential-integrity pass before algorithms; invalid references generally fail later through lookups or `.at()` calls.

## 4. CSV schemas

### 4.1 `corridors.csv`

```csv
id,name
C1,Delhi-Ghaziabad
```

| Field | C++ field | Role |
|---|---|---|
| `id` | `Corridor.id` | referenced by tasks, trains, availability |
| `name` | `Corridor.name` | UI label |

### 4.2 `tasks.csv`

```csv
id,corridor_id,department,task_type,duration_slots,severity,criticality,due_slot,mandatory,requires_power_block,earliest_slot,latest_end_slot
```

| Field | Type/values | Implemented use |
|---|---|---|
| `id` | string | placement key, dependencies, priority join |
| `corridor_id` | corridor ID | fixes the task to one corridor |
| `department` | `ENGINEERING`, `ST`, `TRD` | departmental baseline and UI lanes |
| `task_type` | string | compatibility lookup |
| `duration_slots` | positive integer | continuous placement length |
| `severity` | integer, generated 4–9 | critical classification and ML feature |
| `criticality` | integer, generated 4–10 | critical classification and ML feature |
| `due_slot` | integer | hard latest end for critical tasks; soft lateness otherwise |
| `mandatory` | Boolean text | makes the task critical; all tasks are compulsory regardless |
| `requires_power_block` | Boolean text | metadata/UI only in current scheduling logic |
| `earliest_slot` | integer | lower task-domain bound and priority-delay origin |
| `latest_end_slot` | integer | upper task-domain bound, clipped to horizon |

“Mandatory” does not mean other tasks are optional. Every row must be scheduled exactly once. It only contributes to critical classification and therefore due-date hardness.

The CSV has no `priority_score`. Priority is generated separately at request time so all algorithms consume one explicit batch.

### 4.3 `trains.csv`

```csv
id,corridor_id,start_slot,end_slot,conflict_mode,impact_weight
```

| Field | Meaning |
|---|---|
| `id` | movement identifier |
| `corridor_id` | affected corridor |
| `start_slot`,`end_slot` | movement interval |
| `conflict_mode` | `HARD` forbidden or `SOFT` costly-but-feasible |
| `impact_weight` | contribution when a SOFT movement is impacted |

All trains are electric by domain assumption. There is deliberately no traction/diesel column. HARD movement weights exist in generated CSV but are not included in objective metrics because HARD overlap is forbidden.

### 4.4 `availability.csv`

```csv
corridor_id,start_slot,end_slot
```

Rows are allowed corridor intervals. A task placement must have continuous coverage over its full duration. The validator can treat overlapping/touching availability rows as continuous coverage.

### 4.5 `dependencies.csv`

```csv
predecessor_task_id,successor_task_id,min_lag_slots
```

The implemented rule is:

```text
predecessor.end_slot + min_lag_slots <= successor.start_slot
```

Dependencies are hard constraints. The generator produces simple forward, same-corridor links; the loader itself does not detect arbitrary cycles.

### 4.6 `compatibility.csv`

```csv
task_type_a,task_type_b,compatible
```

The loader sorts each type pair before storing it, so order is irrelevant. Unlisted pairs default to compatible. An incompatible pair cannot overlap on the same corridor, but may be adjacent.

## 5. Temporary ML priority schema

The Go runner performs batch inference and writes a temporary file:

```csv
task_id,priority_score
T001,74.125000
```

The file is created with `os.CreateTemp`, passed to C++ through `--priorities`, and removed with `defer os.Remove` after the command.

`load_priorities` validates:

- at least two columns per row;
- a numeric, finite score in `[0,100]`;
- no duplicate IDs;
- every dataset task has a score;
- no extra/unknown IDs, enforced by equal map/task counts after all tasks match.

After the join, `Task.priority_score` is populated in memory. The stored scenario CSV remains unchanged.

## 6. C++ in-memory schema

`optimizer/include/model.hpp` groups data as:

```text
Dataset
├── vector<Corridor> corridors
├── vector<Task> tasks
├── vector<TrainMovement> trains
├── vector<AvailabilityWindow> availability
├── vector<Dependency> dependencies
└── map<(task_type,task_type),bool> compatibility
```

Important derived structures:

- `CandidateWindows`: map from task ID to free intervals;
- `Placement`: task ID plus selected start/end;
- `Block`: corridor plus union start/end;
- `CandidateEvaluation`: status, train cost, added downtime, reuse;
- `TaskTrace`: compact evaluated-window evidence per task;
- `Plan`: placements, blocks, traces, metrics, validation, status, runtimes.

Placements do not carry corridor ID. The task ID resolves the authoritative task/corridor, preventing a returned placement from independently claiming another corridor.

## 7. Detailed candidate-window transformation

For task `T`, let:

```text
E = max(0,T.earliest_slot)
L = min(T.latest_end_slot,2688)
```

If `T` is critical and has a valid due slot, set `L=min(L,T.due_slot)`. If `L-E < duration`, the candidate list is empty.

For each matching availability `A`:

```text
a0 = max(E,A.start_slot)
a1 = min(L,A.end_slot)
```

Discard when `a1-a0 < duration`.

For each HARD train `H` on the corridor:

```text
h0 = max(a0,H.start_slot)
h1 = min(a1,H.end_slot)
```

Keep when `h0<h1`, then sort by start/end and merge. Subtract the merged forbidden union from `[a0,a1)`. Keep only gaps with length at least `duration`.

Finally, sort all task free windows and merge touching/overlapping windows. The result contains maximal legal intervals after the implemented task, critical-due, corridor-availability, and HARD-train filters.

### Example

Suppose:

```text
task window:       [100,140)
duration:          8 slots
availability:      [96,130), [130,150)
HARD trains:       [108,112), [111,116), [132,134)
SOFT train:        [120,122)
```

The first two HARD intervals merge to `[108,116)`. SOFT `[120,122)` is ignored during preprocessing. Legal free intervals are:

```text
[100,108)  length 8
[116,132)  length 16
[134,140)  length 6 → removed
```

Candidates from `[100,108)` include start 100 only. From `[116,132)`, an 8-slot task normally starts at 116, 118, 120, 122, 124; the latest legal start 124 is already included.

## 8. Preprocessing versus validation

Preprocessing defines domains for algorithms, but validation recomputes hard feasibility independently.

`independently_hard_feasible`:

1. gathers and sorts corridor availability;
2. walks coverage from placement start to end, allowing touching windows;
3. rejects a coverage gap;
4. independently checks all HARD movements for overlap.

`validate` additionally checks uniqueness/completeness, exact duration, task/effective window, incompatibility, and dependencies. A preprocessing bug should therefore be caught when it yields an illegal final placement.

## 9. Preprocessing runtime boundary

Each algorithm times its first `generate_candidate_windows` call as `preprocessing_ms`.

During `finalize`, `build_task_traces` calls `generate_candidate_windows` again to produce explanation evidence. That second call is not added to `preprocessing_ms`, but it is included in `total_runtime_ms`.

Scenario CSV loading, ML feature extraction/inference, temporary priority generation, `load_priorities`, and `load_weights` happen before `solve_*` starts and are excluded from all C++ plan runtimes.

## 10. Generated scenario data

`tools/generate_demo.py` owns deterministic CSV construction. `make generate` supplies:

| Dataset | Seed | Corridors | Tasks | Trains/day |
|---|---:|---:|---:|---:|
| Alpha | 26027 | 10 | 110 | 105 |
| Beta | 26127 | 10 | 124 | 130 |
| Gamma | 26227 | 10 | 120 | 115 |

Task generation details:

- round-robin corridor assignment;
- profile-specific department cycles;
- task type cycles within each department;
- duration from `{2,4,6,8,12}` slots;
- round assigned to one of four weeks;
- legal window length sampled from 5, 6, or 7 days;
- roughly 10% critical and 10% high-priority input characteristics;
- first eight tasks mandatory for these scenario sizes;
- TRD power-work targets: 12% Alpha, 22% Beta, 25% Gamma.

Availability is identical in shape for each corridor/day:

```text
[00:00,06:00), [10:00,17:00), [20:30,24:00)
```

Train generation distributes movements by corridor and time pattern, adds seeded jitter, sets about one patterned fifth to HARD, chooses duration 1–2 slots, uses HARD weight 10, and samples SOFT weight from `{1,3,5,7}`.

Dependency targets are 8%, 10%, and 15% of tasks for Alpha/Beta/Gamma. Compatibility rows are fixed by the generator.

## 11. API JSON schemas

### Dataset catalog

`GET /api/datasets`:

```json
{
  "default_dataset_id": "scenario-alpha",
  "datasets": [{
    "id": "scenario-alpha",
    "label": "Scenario Alpha",
    "description": "...",
    "default": true,
    "task_count": 110,
    "corridor_count": 10,
    "train_movement_count": 2940
  }]
}
```

### Raw selected dataset

`GET /api/dataset?dataset_id=...` includes horizon/all-electric fields plus arrays named after all six CSVs. Every cell value is a JSON string because Go uses `map[string]string`.

### Benchmark envelope

Go augments C++ benchmark JSON with:

```text
dataset_id
priority_model              persisted metadata object
task_priorities[]           task_id, score, source, model version
plans[].dataset_id
plans[].priority_source
plans[].priority_model_version
```

Each C++ plan contains solver identity/status, runtime fields, validation, metrics, placements, blocks, and task traces. `metrics` now includes `priority_weighted_delay`.

The single-plan endpoint places `priority_source` and `priority_model_version` on the top-level plan while also returning `priority_model` and `task_priorities`.

## 12. PostgreSQL relational schema

`db/migrations/001_init.sql` creates:

### Master/input-oriented tables

- `corridors`: text primary key and name.
- `assets`: UUID, corridor FK, department enum, type, unique code, criticality 1–10.
- `maintenance_tasks`: task/corridor/optional asset, department/source/type, duration minutes, severity/criticality, timestamps, mandatory/power flags, status.
- `train_movements`: movement number, corridor, category, timestamps, HARD/SOFT mode, impact weight; no traction field.
- `corridor_availability`: UUID, corridor and time interval.
- `task_dependencies`: predecessor/successor FKs and nonnegative lag minutes.

### Plan/output-oriented tables

- `plans`: dataset ID, horizon, algorithm enum, solver status, three runtime values, objective, JSON metrics/validation/weights, creation time.
- `blocks`: plan/corridor FKs and time interval.
- `block_tasks`: many-to-many block/task association.
- `benchmark_runs`: dataset ID, complete JSON result, creation time.

`002_dataset_ids.sql` upgrades older databases by safely adding/backfilling/not-nulling dataset IDs on `plans` and `benchmark_runs`.

Current `backend/internal/store/postgres.go` implements only:

```sql
INSERT INTO benchmark_runs (dataset_id,result) VALUES ($1,$2)
```

No current migration has columns dedicated to ML model version or priority score. Those values are retained inside benchmark JSON, not normalized tables.

## 13. Data-quality assumptions and gaps

The current runtime assumes:

- scenario task IDs are unique;
- references name existing corridors/tasks;
- intervals have sensible increasing bounds;
- dependency graph is feasible and effectively acyclic;
- compatibility duplicates do not conflict;
- task durations and windows are meaningful;
- ML metadata and model artifact correspond.

The generator satisfies these assumptions, but the C++ CSV loader is not a comprehensive ingestion validator. Production ingestion should add schema validation, referential-integrity diagnostics, cycle detection, versioned dataset manifests, timezone/horizon anchoring, and model/artifact checksums.
