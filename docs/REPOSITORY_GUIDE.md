# Repository structure and ownership

This is a navigation index for the implemented repository. For algorithm and schema details, see [`IMPLEMENTATION_GUIDE.md`](IMPLEMENTATION_GUIDE.md).

## Top-level map

```text
.
├── backend/                       Go REST API and optional PostgreSQL store
│   ├── cmd/api/main.go            composition root and environment loading
│   └── internal/
│       ├── httpapi/               routes, dataset catalog/CSV responses, CORS
│       ├── optimizer/             C++ child-process/JSON boundary
│       └── store/                 pgx benchmark persistence
├── config/                        active objective and explanatory policies
├── data/
│   ├── scenarios/                 Alpha, Beta, Gamma used by API/UI
│   ├── benchmarks/                optional 100/250/500 offline presets
│   └── demo/                      legacy/default CLI-compatible CSV directory
├── db/migrations/                 PostgreSQL schema and dataset-ID upgrade
├── docs/                          team documentation
├── frontend/app/                  Next.js layout, page/components, and CSS
├── optimizer/
│   ├── include/                   shared C++ data and public engine contract
│   ├── src/                       loading, algorithms, validation, JSON CLI
│   └── tests/                     optimizer unit tests
├── scripts/                       setup, development, benchmark workflows
├── tools/                         generator, report converter, native check
├── CMakeLists.txt                 root C++ build option
├── Makefile                       developer entry points
├── docker-compose.yml             PostgreSQL, API, web services
└── README.md                      overview and quick start
```

Generated/downloaded directories include `.deps/`, `build/`, `build-portable/`, `benchmark-results/`, `frontend/.next/`, `frontend/node_modules/`, and `work/`. Do not edit or commit them.

## Runtime boundaries

```text
Browser → Next.js → Go HTTP API → C++ CLI → JSON stdout
                          │
                          └→ PostgreSQL benchmark_runs (optional)
```

- Next.js owns display state, filters, drawers, and HTTP calls.
- Go owns the public API, fixed scenario allowlist, subprocess timeout, JSON checks, and persistence.
- C++ owns CSV/config loading, preprocessing, scheduling, blocks, validation, metrics, runtimes, traces, and JSON.
- PostgreSQL is not optimizer input. CSV under `data/scenarios/{dataset_id}` is reloaded per invocation.

## Root build and runtime files

### `Makefile`

| Target | Current behavior |
|---|---|
| `env` | create `.env` from `.env.example` if missing |
| `install-deps` | `npm ci` and `go mod download` |
| `setup-ortools` | install pinned OR-Tools under `.deps/or-tools` |
| `generate` | reproduce the three stored medium scenarios |
| `generate-presets` | create 100/250/500-task offline datasets |
| `build-optimizer` | native `SIH_WITH_ORTOOLS=ON` build in `build/` |
| `build-portable` | fallback build in `build-portable/` |
| `verify-native` | require native CP-SAT and validator PASS |
| `test` | CTest, Go tests, TypeScript check |
| `benchmark` | scenario Alpha benchmark to JSON and CSV |
| `api`, `web`, `dev` | local services |
| `db-up`, `db-down`, `up`, `down` | Compose workflows |

`.env.example` configures locations and ports, but not scenario identity. `DATA_ROOT` is the parent of all scenarios; each request sends `dataset_id`.

Compose starts PostgreSQL 17, Go plus a portable C++ binary, and standalone Next.js. `backend/Dockerfile` compiles without OR-Tools, so container CP-SAT-shaped results show `native_cp_sat: false`. `frontend/Dockerfile` embeds `NEXT_PUBLIC_API_URL` at build time.

## C++ optimizer

### `optimizer/include/model.hpp`

Defines horizon constants; corridor, task, train, availability, dependency, and compatibility inputs; candidates, placements, blocks, and traces; and `Weights`, `Dataset`, `Metrics`, `ValidationResult`, and `Plan`.

### `optimizer/include/engine.hpp`

Declares `load_dataset`, `load_weights`, `generate_candidate_windows`, `derive_blocks`, `validate`, `calculate_metrics`, all three `solve_*` functions, and JSON serializers.

### `optimizer/src/main.cpp`

Parses:

```text
sih-optimizer <independent|greedy|cp-sat|benchmark>
  --data DIR --config FILE --time-limit SECONDS
```

Defaults are `data/demo`, `config/optimizer.conf`, and 15 seconds.

### `optimizer/src/engine.cpp`

Key symbols are `critical_task`, `effective_latest_end`, `generate_candidate_windows`, `candidate_starts`, `ordered_tasks`, `greedy_schedule`, `departmental_schedule`, `solve_independent`, `solve_greedy`, `solve_cp_sat`, `derive_blocks`, `validate`, `calculate_metrics`, `build_task_traces`, and `finalize`.

The validator rechecks placements against source data rather than solver variables. Every algorithm finishes through the same finalization path.

### Tests and build

`optimizer/tests/engine_test.cpp` tests HARD-interval merging, SOFT feasibility/cost, CP-SAT placement choice, task traces, and rejection of an unscheduled task. `optimizer/CMakeLists.txt` also registers a full Alpha smoke benchmark.

## CSV and configuration ownership

Each `data/scenarios/*` directory contains:

| File | Implemented fields |
|---|---|
| `corridors.csv` | `id,name` |
| `tasks.csv` | ID, corridor, department, task type, duration slots, severity, criticality, due, mandatory, power flag, earliest, latest end |
| `trains.csv` | ID, corridor, start/end slots, `HARD`/`SOFT`, impact weight |
| `availability.csv` | corridor, allowed start/end slots |
| `dependencies.csv` | predecessor, successor, minimum lag slots |
| `compatibility.csv` | task-type pair and Boolean compatibility |

`config/optimizer.conf` is actively parsed. `config/priority.conf` and `config/train-weights.conf` are explanatory only: current priority is in `priority_score`, and actual train weights are in `trains.csv`.

`tools/generate_demo.py` creates the deterministic scenario CSVs. `scripts/benchmark.sh` benchmarks Alpha and honors `SOLVER_TIME_LIMIT_SECONDS`. `tools/benchmark_report.py` makes CSV. `tools/verify_native_cp_sat.py` requires native CP-SAT and validator PASS. `scripts/dev.sh` starts Go and Next.js with absolute repo paths.

## Go backend

### `backend/cmd/api/main.go`

Reads environment values, opens PostgreSQL if configured, constructs `optimizer.CommandRunner`, and serves `httpapi.New`. Database connection failure is fail-open.

### `backend/internal/httpapi/server.go`

Defines routes, the fixed Alpha/Beta/Gamma catalog, default Alpha, `dataset_id` parsing/resolution, CSV-to-JSON, and benchmark persistence.

| Route | Behavior |
|---|---|
| `GET /api/health` | status and horizon constants |
| `GET /api/datasets` | catalog plus corridor/task/train counts |
| `GET /api/dataset` | all six selected CSV files as row maps |
| `GET/POST /api/benchmark` | all three algorithms on selected dataset |
| `GET /api/plans/{algorithm}` | one allowlisted algorithm |

### `backend/internal/optimizer/runner.go`

`CommandRunner.run` adds a five-second grace period to the solver limit, passes CLI arguments, rejects process/JSON failures, and injects `dataset_id` into the outer result and benchmark plans. Accepted algorithms are `independent`, `greedy`, and `cp-sat`.

### `backend/internal/store/postgres.go`

The only implemented write is `INSERT INTO benchmark_runs (dataset_id, result) VALUES ($1, $2)`.

`backend/internal/httpapi/server_test.go` tests health, database-free benchmark behavior, the three medium scenarios, explicit selection, and unknown-ID rejection.

## PostgreSQL

`001_init.sql` creates enums and `corridors`, `assets`, `maintenance_tasks`, `train_movements`, `corridor_availability`, `task_dependencies`, `plans`, `blocks`, `block_tasks`, and `benchmark_runs`. Foreign keys connect assets/tasks to corridors, dependencies to tasks, blocks to plans/corridors, and block-task rows to both parents.

`002_dataset_ids.sql` adds/backfills/non-nulls `dataset_id` on `plans` and `benchmark_runs` for upgraded databases. `001` already contains those columns for fresh databases, so `002` is an idempotent guard.

Only `benchmark_runs` is written today. Normalized planning tables are neither loaded from CSV nor populated by the API.

## Next.js frontend

`frontend/app/page.tsx` is a client component containing:

1. `Overview` — CP-SAT summary, KPIs, savings, weekly rhythm;
2. `Planner` — algorithm/week/corridor/department controls and train/block/task lanes;
3. `Tasks` — searchable/filterable monthly register;
4. `Verification` — validator result, violation categories, runtime split;
5. `BenchmarkPage` — same-dataset cost/runtime charts and metric table.

It also contains `TaskDrawer`, `CandidateTimeline`, and `BlockDrawer`. `Home` loads the catalog, then requests benchmark and dataset JSON in parallel and rejects mismatched `dataset_id` values.

`globals.css` owns the responsive visual system; `layout.tsx` owns metadata; `next.config.ts` enables standalone output. The npm `lint` script is `tsc --noEmit`, not ESLint.

## Change map

| Change | Primary locations |
|---|---|
| Horizon/input/result field | `model.hpp`, `engine.cpp`, Go/TypeScript types |
| Candidate or hard rule | `engine.cpp`, `engine_test.cpp`, UI verification copy |
| Objective weights | `config/optimizer.conf` |
| Priority formula | `priority_score` in `engine.cpp` |
| Scenario generation/count | generator, `Makefile`, API descriptions |
| Dataset allowlist/API | `server.go`, `server_test.go` |
| Subprocess behavior | `runner.go` |
| Persistence | migrations and `store/postgres.go` |
| UI | `page.tsx`, `globals.css` |
| Benchmark columns | `benchmark_report.py` |

When JSON changes, update C++, Go, TypeScript, report conversion, tests, and docs together.
