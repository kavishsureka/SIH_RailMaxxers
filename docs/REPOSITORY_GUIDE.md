# Repository structure, architecture, and data flow

This document explains how to navigate the implementation. It focuses on file ownership, process boundaries, and runtime flow rather than repeating the planning concepts in the reference material.

## 1. Top-level map

```text
.
├── backend/                    Go REST API and optional PostgreSQL persistence
│   ├── cmd/api/                API executable entry point
│   └── internal/               HTTP, optimizer-process, and storage packages
├── config/                     Runtime and policy configuration
├── data/demo/                  Committed synthetic CSV input dataset
├── db/migrations/              PostgreSQL schema
├── docs/                       Team-facing documentation
├── frontend/                   Next.js browser dashboard
│   └── app/                    Page, layout, and global styling
├── optimizer/                  C++ algorithms, validator, metrics, and CLI
│   ├── include/                Shared structures and public functions
│   └── src/                    CLI and implementation
├── scripts/                    Repeatable shell workflows
├── tools/                      Dataset and report generators
├── CMakeLists.txt              Root C++ build configuration
├── Makefile                    Common developer commands
├── docker-compose.yml          PostgreSQL + API + frontend stack
└── README.md                   Overview and short quick start
```

Generated directories such as `build/`, `frontend/.next/`, `frontend/node_modules/`, `benchmark-results/`, and `work/` are not source code. Do not edit or commit them.

## 2. Runtime architecture

```text
Browser
  │ HTTP on :3000
  ▼
Next.js dashboard
  │ JSON REST requests to :8080
  ▼
Go API
  ├── reads CSV directly for /api/dataset
  ├── launches C++ for plans and benchmarks
  └── optionally stores benchmark JSON in PostgreSQL
            │
            │ command arguments + stdout JSON
            ▼
       C++ optimizer CLI
         ├── loads CSV data and objective config
         ├── runs the requested algorithm(s)
         ├── derives shared blocks
         ├── validates each plan
         ├── calculates metrics and runtime
         └── writes JSON to stdout
```

The boundary is deliberate: Go owns HTTP and persistence, while C++ owns scheduling truth. The frontend never invokes C++ directly, and Go does not duplicate algorithms, block derivation, validation, or objective calculation.

## 3. Root files

### `CMakeLists.txt`

Root CMake entry point. It declares the project, exposes `SIH_WITH_ORTOOLS`, enables CTest, and includes the optimizer subdirectory.

Change it when adding another C++ subproject or repository-wide CMake option.

### `Makefile`

The Makefile loads the root `.env` and provides short, repeatable workflows:

- `setup` installs dependencies, native OR-Tools, builds, and verifies;
- `setup-ortools` installs the pinned C++ distribution under `.deps/`;
- `generate` regenerates `data/demo/`;
- `build-optimizer` configures and compiles native CP-SAT;
- `build-portable` creates a separate diagnostic fallback binary;
- `verify-native` proves the runtime is OR-Tools-backed and validator-approved;
- `build-api` compiles Go;
- `test` runs C++, Go, and frontend checks;
- `benchmark` creates JSON and CSV reports;
- `dev` starts the API and frontend together;
- `api` runs the local backend;
- `web` runs the frontend development server;
- `up` and `down` wrap Docker Compose.

Keep complex behavior in scripts or language-specific build files rather than growing long Make recipes.

### `.env.example`

Tracked template for each teammate's ignored personal `.env`. The Makefile exports these values to build and runtime commands.

### `.gitignore`

Excludes dependencies, compiled files, temporary work, environment secrets, and generated reports.

### `docker-compose.yml`

Defines:

- `postgres`: database with one-time initialization from `db/migrations/`;
- `api`: Go API plus portable C++ optimizer;
- `web`: production Next.js build.

It also owns container ports, development database credentials, health checks, startup order, and the `postgres-data` volume.

## 4. C++ optimizer

```text
optimizer/
├── CMakeLists.txt
├── ORTOOLS.md
├── include/
│   ├── engine.hpp
│   └── model.hpp
└── src/
    ├── engine.cpp
    └── main.cpp
```

### `optimizer/include/model.hpp`

Shared in-memory types used by every algorithm and the validator:

- horizon constants;
- corridor, task, train, availability, dependency, and compatibility input records;
- placement and derived block output records;
- objective weights;
- dataset, metrics, validation result, and plan aggregates.

Start here when adding a field that must travel through C++.

### `optimizer/include/engine.hpp`

Public C++ function contract for:

- loading data and weights;
- deriving blocks;
- validation and metrics;
- the three solver entry points;
- plan and benchmark JSON serialization.

Go does not link this header. It communicates with the compiled executable.

### `optimizer/src/main.cpp`

Thin CLI adapter. It parses the command/options, loads inputs, dispatches `independent`, `greedy`, `cp-sat`, or `benchmark`, prints JSON to stdout, and sends failures to stderr with a non-zero exit code.

Add a new top-level optimizer command here, but keep algorithm logic in `engine.cpp`.

### `optimizer/src/engine.cpp`

The scheduling source of truth. It contains:

- CSV and configuration parsing;
- overlap, availability, train-conflict, compatibility, and placement helpers;
- task ordering and shared greedy placement;
- dataset loading;
- contiguous block derivation;
- independent validation;
- common KPI and objective calculation;
- Independent and Coordinated Greedy algorithms;
- native OR-Tools model under `SIH_WITH_ORTOOLS`;
- portable fallback under the opposite compile branch;
- JSON serialization.

All algorithms should finish through the shared `finalize` path. That guarantees identical block derivation, validation, metrics, objective calculation, and runtime reporting.

### `optimizer/CMakeLists.txt`

Builds `sih-optimizer` and registers the smoke benchmark. With `SIH_WITH_ORTOOLS=ON`, it finds and links `ortools::ortools` and activates the native compile branch.

### `optimizer/ORTOOLS.md`

Focused native OR-Tools build and verification instructions.

## 5. Input data and configuration

### `data/demo/corridors.csv`

Corridor IDs and names. Other files reference these IDs.

### `data/demo/tasks.csv`

Task records: corridor, department, type, duration in slots, scoring inputs, due slot, mandatory/power flags, and legal task window.

### `data/demo/trains.csv`

Movement windows, hard/soft conflict mode, and impact weight. It intentionally has no traction field.

### `data/demo/availability.csv`

Allowed intervals by corridor. A whole task interval must fit inside one listed window.

### `data/demo/dependencies.csv`

Predecessor/successor pairs and minimum lag in fifteen-minute slots.

### `data/demo/compatibility.csv`

Pairwise task-type compatibility used by the optimizer and validator. Unlisted pairs default to compatible.

### `config/optimizer.conf`

Active runtime configuration parsed by C++. It defines `wB`, `wD`, `wT`, `wO`, and `wC`.

### `config/priority.conf`

Documents the prototype priority policy. It is not currently loaded as an independent runtime file; active ordering is in the C++ helper. If the policy becomes configurable, add a loader and remove the duplication.

### `config/train-weights.conf`

Documents category defaults. Active per-movement weights are stored in `trains.csv`; the generator assigns them.

## 6. Data and report tools

### `tools/generate_demo.py`

Deterministic standard-library generator. It owns corridor/task/train counts, task distributions and windows, traffic windows and weights, availability, dependencies, and incompatibilities.

Change it when the synthetic scenario shape changes. Use a fixed seed for reproducible teamwork.

### `scripts/benchmark.sh`

Runs the compiled C++ benchmark, writes `benchmark-results/latest.json`, and starts CSV conversion.

### `scripts/setup-ortools.sh`

Detects the supported operating system and CPU, downloads the pinned official OR-Tools C++ archive, validates its layout, and installs it under the `.env`-controlled `ORTOOLS_ROOT`.

### `scripts/dev.sh`

Loads `.env`, resolves repository paths, starts the Go API in the background, runs Next.js in the foreground, and stops the API when development ends.

### `tools/verify_native_cp_sat.py`

Executes the CP-SAT CLI and fails unless `native_cp_sat` is true and the independent validator accepts the returned plan.

### `tools/benchmark_report.py`

Flattens nested benchmark JSON into one CSV row per algorithm. Add report columns here when adding metrics.

## 7. Go backend

```text
backend/
├── Dockerfile
├── go.mod
├── go.sum
├── cmd/api/main.go
└── internal/
    ├── httpapi/
    │   ├── server.go
    │   └── server_test.go
    ├── optimizer/runner.go
    └── store/postgres.go
```

### `backend/cmd/api/main.go`

Backend composition root. It reads environment variables, optionally opens PostgreSQL, constructs the optimizer runner, creates the HTTP handler, and listens on `API_ADDR`.

Wire new backend dependencies here rather than implementing request behavior here.

### `backend/internal/httpapi/server.go`

Owns routing, CORS, JSON responses, `/api/dataset` CSV exposure, optimizer calls, and optional benchmark persistence.

| Route | Behavior |
|---|---|
| `GET /api/health` | Service and horizon contract |
| `GET /api/dataset` | Reads and returns demo CSV files |
| `GET /api/benchmark` | Runs every algorithm |
| `POST /api/benchmark` | Same benchmark execution path |
| `GET /api/plans/{algorithm}` | Runs one accepted algorithm |

Add endpoints here, splitting into smaller handler files if the package grows.

### `backend/internal/httpapi/server_test.go`

HTTP tests with an in-memory fake optimizer. They test routing without C++ or PostgreSQL.

### `backend/internal/optimizer/runner.go`

The Go-to-C++ boundary. It defines result JSON types, the testable `Runner` interface, child-process timeout, executable arguments, stdout JSON validation, and algorithm-name allowlisting.

If transport changes to gRPC or a library binding later, this package is the primary replacement boundary.

### `backend/internal/store/postgres.go`

Small `pgxpool` adapter. It opens/checks the connection and inserts full benchmark JSON into `benchmark_runs`.

Persistence is behind `BenchmarkStore`. A nil store means database-free mode.

### `backend/go.mod` and `backend/go.sum`

Declare and lock Go dependencies. Run `go mod tidy` after import changes.

### `backend/Dockerfile`

Multi-stage build. It compiles the portable C++ optimizer and Go API, then copies both binaries plus data/config into a small non-root runtime image.

## 8. PostgreSQL

### `db/migrations/001_init.sql`

Creates the initial enums, tables, constraints, relationships, and benchmark JSON store.

| Table | Responsibility |
|---|---|
| `corridors` | Corridor master records |
| `assets` | Department-owned assets |
| `maintenance_tasks` | Persistable tasks |
| `train_movements` | Persistable traffic windows |
| `corridor_availability` | Persistable allowed windows |
| `task_dependencies` | Precedence relationships |
| `plans` | Plan status, runtime, KPI, validation, and weights |
| `blocks` | Consolidated intervals for a plan |
| `block_tasks` | Block-to-task many-to-many link |
| `benchmark_runs` | Raw result JSON used by the current Go adapter |

The optimizer currently loads CSV input rather than building `Dataset` from normalized tables. PostgreSQL is wired for benchmark persistence and provides the forward schema for later database-backed planning. Editing a database task does not yet change optimizer input.

## 9. Next.js frontend

```text
frontend/
├── app/
│   ├── globals.css
│   ├── layout.tsx
│   └── page.tsx
├── Dockerfile
├── next.config.ts
├── package.json
├── package-lock.json
└── tsconfig.json
```

### `frontend/app/page.tsx`

Single dashboard page containing:

- TypeScript types matching benchmark JSON;
- API base URL selection;
- slot-to-weekday/time formatting;
- loading and error state;
- active-algorithm selection;
- comparison cards;
- weekly Gantt;
- KPI and validator panels.

Components are colocated for prototype speed. Split API helpers, cards, and charts when more screens or tests are added.

### `frontend/app/globals.css`

Design tokens, layout, Gantt styling, interaction states, and responsive breakpoint.

### `frontend/app/layout.tsx`

Root App Router layout and page metadata.

### `frontend/next.config.ts`

Enables standalone production output for the runtime container.

### `frontend/package.json` and `package-lock.json`

Scripts and locked npm dependencies. Teammates and CI should use `npm ci`.

### `frontend/tsconfig.json`

Strict TypeScript and Next.js compiler configuration.

### `frontend/Dockerfile`

Installs locked packages, compiles with `NEXT_PUBLIC_API_URL`, and runs the standalone server.

## 10. End-to-end data flows

### Dashboard benchmark

```text
page.tsx
  → GET /api/benchmark
  → httpapi.Server.benchmark
  → optimizer.CommandRunner.Benchmark
  → sih-optimizer benchmark --data ... --config ...
  → load CSV/config
  → run Independent, Greedy, CP-SAT
  → finalize plans through blocks + validator + metrics + runtime
  → serialize JSON to stdout
  → Go validates JSON
  → optional benchmark_runs insert
  → HTTP response
  → React state update and render
```

### Single plan

```text
GET /api/plans/greedy
  → algorithm allowlist
  → C++ `greedy` command
  → one finalized Plan object
  → JSON response
```

### Dataset inspection

```text
GET /api/dataset
  → Go reads every CSV directly
  → rows become key/value JSON objects
  → response adds horizon and all-electric metadata
```

This endpoint does not launch C++.

### Dataset regeneration

```text
make generate
  → tools/generate_demo.py
  → overwrite data/demo/*.csv
  → next optimizer invocation reloads the files
```

### Offline benchmark reports

```text
make benchmark
  → compile optimizer
  → scripts/benchmark.sh
  → latest.json
  → tools/benchmark_report.py
  → latest.csv
```

## 11. C++ to Go output contract

High-level benchmark JSON:

```json
{
  "horizon_slots": 672,
  "slot_minutes": 15,
  "plans": [
    {
      "algorithm": "greedy",
      "solver_status": "FEASIBLE",
      "runtime_ms": 12.5,
      "native_cp_sat": false,
      "validation": {"valid": true, "violations": []},
      "metrics": {},
      "placements": [],
      "blocks": []
    }
  ]
}
```

When changing this contract:

1. update C++ structures and serialization;
2. update Go types if Go decodes the field;
3. update frontend TypeScript types and rendering;
4. update CSV conversion for report metrics;
5. rerun C++, Go, and frontend checks.

## 12. Where to make common changes

| Desired change | Primary files |
|---|---|
| Change objective weights | `config/optimizer.conf` |
| Edit committed demo records | `data/demo/*.csv` |
| Change generated scenario shape | `tools/generate_demo.py` |
| Add a C++ field | `model.hpp`, `engine.cpp` |
| Change validator behavior | `engine.cpp` in `validate`, plus tests |
| Change an algorithm | `optimizer/src/engine.cpp` |
| Add an optimizer command | `main.cpp`, `engine.hpp` |
| Add an API route | `server.go`, `server_test.go` |
| Change Go-to-C++ invocation | `runner.go` |
| Expand persistence | `db/migrations/`, `internal/store/` |
| Change dashboard behavior | `frontend/app/page.tsx` |
| Change visual styling | `frontend/app/globals.css` |
| Add benchmark CSV fields | `tools/benchmark_report.py` |
| Change containers | `docker-compose.yml`, Dockerfiles |

## 13. Contributor navigation rules

- Treat `optimizer/src/engine.cpp` as the scheduling source of truth.
- Do not implement a second validator in Go or TypeScript.
- Do not edit generated build/dependency folders.
- Update all consumers together when changing JSON.
- Preserve database-optional behavior unless the team deliberately changes that contract.
- Keep `native_cp_sat` visible so fallback and native results cannot be confused.
- State whether new configuration is actively parsed or only documents defaults.
- Update this guide after structural changes so teammates do not need to reverse-engineer the repository.
