# Setup, running, testing, and troubleshooting

RailBlock supports local development with or without PostgreSQL and a full Docker Compose stack. Local native OR-Tools is the correct path for solver demonstrations; the API container uses the labelled portable fallback.

## Requirements

| Tool | Version used/required |
|---|---|
| CMake | 3.20+ |
| C++ compiler | C++20 capable |
| Go | 1.23+ (`backend/go.mod`) |
| Node.js | 22+ |
| npm | lockfile installation with `npm ci` |
| Python | Python 3 |
| PostgreSQL | optional; Compose uses 17 |
| Docker | optional |

Windows developers should use WSL 2 for native development.

## One-time local setup

```bash
git clone <repository-url>
cd <repository-directory>
cp .env.example .env
make setup
```

`make setup` creates `.env` if needed, installs frontend/Go dependencies, downloads pinned OR-Tools 9.12 to `.deps/or-tools`, builds the native optimizer, and runs `tools/verify_native_cp_sat.py`. Verification requires `native_cp_sat: true` and an independently valid CP-SAT result.

Important defaults in `.env.example`:

```dotenv
DATABASE_URL=
API_ADDR=:8080
OPTIMIZER_BIN=../build/optimizer/sih-optimizer
DATA_ROOT=../data/scenarios
OPTIMIZER_CONFIG=../config/optimizer.conf
NEXT_PUBLIC_API_URL=http://localhost:8080
ORTOOLS_ROOT=.deps/or-tools
ORTOOLS_VERSION=9.12
SOLVER_TIME_LIMIT_SECONDS=15
CMAKE_BUILD_PARALLEL_LEVEL=4
```

`DATA_ROOT` is only a base directory. The UI/API supplies `dataset_id` at runtime; never switch Alpha/Beta/Gamma by editing `.env`.

## Stored datasets

The committed live scenarios are:

```text
data/scenarios/scenario-alpha   110 tasks, 2,940 trains
data/scenarios/scenario-beta    124 tasks, 3,640 trains
data/scenarios/scenario-gamma   120 tasks, 3,220 trains
```

Regenerate them deterministically:

```bash
make generate
```

The exact profile, seed, corridor/task/train counts are in the three recipes under `Makefile: generate`. Separate 100/250/500-task offline presets are created with `make generate-presets`; they are not UI options.

## Build and direct CLI use

```bash
make build-optimizer
make verify-native
```

The native executable is `build/optimizer/sih-optimizer`.

```bash
./build/optimizer/sih-optimizer benchmark \
  --data data/scenarios/scenario-alpha \
  --config config/optimizer.conf \
  --time-limit 15
```

Commands are `independent`, `greedy`, `cp-sat`, and `benchmark`. The result reports solver status, native/fallback identity, validation, metrics, placements, blocks, task traces, and preprocessing/algorithm/total runtimes.

For troubleshooting without OR-Tools:

```bash
make build-portable
./build-portable/optimizer/sih-optimizer cp-sat \
  --data data/scenarios/scenario-alpha \
  --config config/optimizer.conf
```

That result says `FALLBACK_FEASIBLE` and `native_cp_sat: false`; do not present it as native CP-SAT.

## Local development without PostgreSQL

Leave `DATABASE_URL=` blank and run:

```bash
make dev
```

Open `http://localhost:3000`; the API is at `http://localhost:8080`. For separate logs:

```bash
# terminal 1
make api

# terminal 2
make web
```

Planning, validation, dataset switching, and benchmark comparison work without a database. Only benchmark history persistence is absent.

## API checks

```bash
curl http://localhost:8080/api/health
curl http://localhost:8080/api/datasets
curl 'http://localhost:8080/api/dataset?dataset_id=scenario-alpha'
curl 'http://localhost:8080/api/plans/greedy?dataset_id=scenario-beta'
curl 'http://localhost:8080/api/benchmark?dataset_id=scenario-gamma'
curl -X POST -H 'Content-Type: application/json' \
  -d '{"dataset_id":"scenario-gamma"}' \
  http://localhost:8080/api/benchmark
```

Expected health fields are `status: ok`, `slot_minutes: 15`, `horizon_days: 28`, and `horizon_weeks: 4`. Missing IDs default to Alpha; unknown IDs return HTTP 400.

The Go process timeout is the solver time limit plus five seconds. If no valid positive duration reaches `CommandRunner`, its defensive process timeout is 15 seconds.

## Local PostgreSQL

Start only the Compose database:

```bash
make db-up
```

Set:

```dotenv
DATABASE_URL=postgres://railblock:railblock@localhost:5432/railblock?sslmode=disable
```

Then restart `make dev`. Each benchmark request inserts its selected `dataset_id` and full JSON into `benchmark_runs`. No other normalized planning table is written by current Go code.

For a locally installed PostgreSQL server, create the database/user and apply both migrations in order:

```bash
psql "$DATABASE_URL" -f db/migrations/001_init.sql
psql "$DATABASE_URL" -f db/migrations/002_dataset_ids.sql
```

Compose mounts the migration directory into `/docker-entrypoint-initdb.d`; scripts run only when its volume is first initialized.

Inspect saved runs:

```bash
docker compose exec postgres psql -U railblock -d railblock \
  -c 'SELECT id, dataset_id, created_at FROM benchmark_runs ORDER BY created_at DESC LIMIT 5;'
```

`docker compose down -v` permanently deletes the development database volume; use it only when a reset is intentional.

## Full Docker stack

```bash
docker compose up --build
```

| Service | Address |
|---|---|
| Next.js | `http://localhost:3000` |
| Go API | `http://localhost:8080` |
| PostgreSQL | `localhost:5432` |

The API image compiles without OR-Tools and exposes the portable fallback. Use local `make build-optimizer`/`make dev` for a native CP-SAT demo.

## Tests and benchmark artifacts

```bash
make test
```

This rebuilds native C++, runs CTest (`optimizer_smoke`, `optimizer_unit`), runs `go test ./...`, and runs the frontend TypeScript check (`npm run lint` → `tsc --noEmit`). It does not run `next build` or browser automation.

Useful individual checks:

```bash
ctest --test-dir build --output-on-failure
cd backend && go test ./...
cd frontend && npm run lint
cd frontend && npm run build
```

Run the checked-in report pipeline:

```bash
make benchmark
SOLVER_TIME_LIMIT_SECONDS=30 make benchmark
```

It always uses `scenario-alpha` and writes `benchmark-results/latest.json` and `latest.csv`. The directory is ignored by Git.

## UI acceptance checklist

1. Catalog loads Alpha/Beta/Gamma and Alpha is selected by default.
2. Switching scenarios refreshes both benchmark and raw dataset and keeps IDs consistent.
3. Overview shows the CP-SAT recommendation and 28-day completion.
4. Block Planner switches algorithm and week, filters corridor/department, and opens task/block drawers.
5. Maintenance Tasks search/filters and task drawer work.
6. Plan Verification separates solver status from validator PASS/FAIL.
7. Benchmark shows all three plans, one dataset banner, weighted metrics, and all three runtime components.
8. **Run benchmark** reruns the currently selected scenario.
9. Browser console has no errors and narrow layout remains usable.

There is no automated browser suite.

## Tunable configuration

| Value | Exact location | Effect |
|---|---|---|
| `wB,wD,wT,wL,wV` | `config/optimizer.conf`; loaded by `load_weights` | weighted objective |
| CP-SAT limit | `SOLVER_TIME_LIMIT_SECONDS` / CLI `--time-limit` | solver time budget |
| workers/seed | `solve_cp_sat` in `optimizer/src/engine.cpp` | fixed at 8 / 26027 |
| candidate step | `candidate_starts` in `engine.cpp` | two slots (30 minutes) |
| priority formula | `priority_score` in `engine.cpp` | heuristic ordering |
| scenarios/seeds | `Makefile: generate` | stored demo shape |
| generation distributions | `tools/generate_demo.py` | tasks/trains/windows/dependencies |
| scenario allowlist | `datasetDefinitions` in `server.go` | API-visible datasets |

`config/priority.conf` and `config/train-weights.conf` are not loaded at runtime.

## Troubleshooting

### OR-Tools is missing or native verification fails

```bash
make setup-ortools
make build-optimizer
make verify-native
```

Expected package file: `.deps/or-tools/lib/cmake/ortools/ortoolsConfig.cmake`.

### An interrupted dependency download exists

Move the incomplete `.deps/or-tools` directory out of the repository, then rerun `make setup-ortools`. The script will not overwrite it.

### macOS blocks downloaded OR-Tools libraries

Approve the trusted official archive in Privacy & Security, then restrict quarantine removal to the local dependency:

```bash
xattr -dr com.apple.quarantine .deps/or-tools
make verify-native
```

### API says optimizer failed/not found

Run `make build-optimizer`. If starting Go manually, verify paths are relative to `backend/`, or use the absolute exports in `scripts/dev.sh`.

### Dashboard says API offline

Check `/api/health`. If the API port changes, update `NEXT_PUBLIC_API_URL` and restart/rebuild Next.js because production builds embed it.

### PostgreSQL warning but API starts

This is intentional fail-open behavior. Fix/clear `DATABASE_URL` and restart. Planning remains available.

### Tables are missing

Compose initialization only runs on an empty volume. Apply both migrations or intentionally recreate the development volume.

### CSV edits are not visible

Verify the edited folder matches the selected `dataset_id`, then click **Run benchmark**. Each request reloads files; the database is not the input source.

### CP-SAT reports `native_cp_sat: false`

You are using `build-portable` or the Docker API binary. Point `OPTIMIZER_BIN` to `build/optimizer/sih-optimizer` and run `make verify-native`.
