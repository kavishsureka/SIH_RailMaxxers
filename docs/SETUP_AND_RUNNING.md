# Setup, running, and testing guide

This guide is for teammates cloning RailBlock from GitHub for the first time. It covers three supported workflows:

1. local development without PostgreSQL;
2. local development with PostgreSQL persistence;
3. the complete stack through Docker Compose.

The fastest development path is **local without PostgreSQL**. PostgreSQL currently stores benchmark run documents, but it is not required for dataset loading, optimization, validation, the REST API, or the dashboard.

## 1. Software requirements

| Tool | Version | Purpose |
|---|---:|---|
| Git | Current stable | Clone and collaborate through GitHub |
| CMake | 3.20+ | Configure the C++ optimizer build |
| C++ compiler | C++20 capable | Compile with `clang++` or `g++` |
| Go | 1.23+ | Build and run the REST API |
| Node.js | 22+ | Build and run Next.js |
| npm | Bundled with Node.js | Install frontend packages |
| Python | 3.10+ | Generate CSV data and benchmark reports |
| PostgreSQL | 15+; Compose uses 17 | Optional persistence |
| Docker | Current stable | Optional container setup |

Windows teammates should use WSL2 for native development. Docker Desktop is also supported. Avoid mixing a Windows-built optimizer with a backend running inside WSL because executable paths differ.

Verify the native toolchain:

```bash
git --version
cmake --version
c++ --version
go version
node --version
npm --version
python3 --version
```

For Docker workflows, also verify:

```bash
docker --version
docker compose version
```

## 2. Clone and install dependencies

```bash
git clone <YOUR_GITHUB_REPOSITORY_URL>
cd <CLONED_REPOSITORY_DIRECTORY>
```

Create your personal configuration and run the one-time native setup:

```bash
cp .env.example .env
make setup
```

`make setup` installs locked frontend and Go dependencies, downloads the pinned OR-Tools C++ distribution to the ignored `.deps/` directory, builds the native optimizer, and verifies that CP-SAT reports `native_cp_sat: true` with a valid independent-validator result.

For the supported platform list and teammate-oriented checklist, see `docs/NATIVE_CPSAT_TEAM_SETUP.md`.

```bash
make install-deps
```

## 3. Demo dataset

The repository already contains deterministic input under `data/demo/`. Regeneration is optional.

```bash
make generate
```

Equivalent command with the shared seed:

```bash
python3 tools/generate_demo.py --seed 26027 --output data/demo
```

This replaces the CSV contents in `data/demo/`. Commit those changes only when the team intentionally wants to update the shared scenario.

## 4. Build and check the optimizer

From the repository root:

```bash
make build-optimizer
make verify-native
```

The executable is created at:

```text
build/optimizer/sih-optimizer
```

Run all three algorithms directly:

```bash
./build/optimizer/sih-optimizer benchmark \
  --data data/demo \
  --config config/optimizer.conf
```

The command prints JSON containing Independent, Greedy, and CP-SAT plan results. Inspect `native_cp_sat` in the CP-SAT result:

- `true` is required for the normal native build;
- `false` appears only in the explicit `make build-portable` troubleshooting build.

## 5. Local run without PostgreSQL

This is the recommended everyday development setup. Benchmark results reach the dashboard but are not saved after the API process stops.

Leave `DATABASE_URL=` blank in `.env`, then start both services:

```bash
make dev
```

For separate logs, use two terminals:

```bash
# Terminal 1
make api

# Terminal 2
make web
```

The API runs at `http://localhost:8080`; open the dashboard at `http://localhost:3000`.

The page should display three algorithm cards, the selected plan's weekly Gantt, KPI values, solver status, mandatory runtime, and validator status.

### Check the API manually

```bash
curl http://localhost:8080/api/health
curl http://localhost:8080/api/dataset
curl http://localhost:8080/api/plans/greedy
curl http://localhost:8080/api/benchmark
```

Expected health response:

```json
{"horizon_days":7,"slot_minutes":15,"status":"ok"}
```

## 6. Local run with PostgreSQL

When `DATABASE_URL` is valid, every `/api/benchmark` result is inserted into `benchmark_runs.result` as JSONB. If PostgreSQL cannot be reached, the API logs a warning and deliberately continues without persistence.

### Option A: PostgreSQL through Docker, app locally

This is the easiest database-enabled developer setup.

```bash
docker compose up -d postgres
docker compose ps
```

Wait until `postgres` is healthy. Development credentials are:

```text
Host:     localhost
Port:     5432
Database: railblock
User:     railblock
Password: railblock
```

The migration runs automatically only when the PostgreSQL volume is initialized for the first time.

Start the API with persistence:

```bash
export DATABASE_URL='postgres://railblock:railblock@localhost:5432/railblock?sslmode=disable'
make api
```

In another terminal:

```bash
make web
```

Trigger a benchmark from the UI or with:

```bash
curl http://localhost:8080/api/benchmark
```

Confirm persistence:

```bash
docker compose exec postgres \
  psql -U railblock -d railblock \
  -c 'SELECT id, created_at FROM benchmark_runs ORDER BY created_at DESC LIMIT 5;'
```

Stop the database without deleting its volume:

```bash
docker compose stop postgres
```

### Option B: locally installed PostgreSQL

Start the local PostgreSQL service. As a PostgreSQL administrator, create the development user and database once:

```sql
CREATE USER railblock WITH PASSWORD 'railblock';
CREATE DATABASE railblock OWNER railblock;
```

Apply the initial schema from the repository root:

```bash
psql 'postgres://railblock:railblock@localhost:5432/railblock?sslmode=disable' \
  -f db/migrations/001_init.sql
```

Start the backend:

```bash
export DATABASE_URL='postgres://railblock:railblock@localhost:5432/railblock?sslmode=disable'
make api
```

The initial migration creates enum types and is not intended to be repeatedly applied to the same database. Introduce a migration tool before adding multiple production migrations.

### Reset the Docker development database

The next command permanently deletes the local Compose database volume. Use it only when a clean database is intentional:

```bash
docker compose down -v
docker compose up -d postgres
```

## 7. Complete Docker Compose stack

Run PostgreSQL, API/optimizer, and frontend together:

```bash
docker compose up --build
```

| Service | Address |
|---|---|
| Frontend | `http://localhost:3000` |
| REST API | `http://localhost:8080` |
| PostgreSQL | `localhost:5432` |

Background mode and logs:

```bash
docker compose up --build -d
docker compose logs -f api web
```

Stop without deleting database data:

```bash
docker compose down
```

The current backend container compiles the portable optimizer. Native OR-Tools is not bundled into the container image.

## 8. Frontend development and testing

### Development server

Start the API first, then:

```bash
cd frontend
npm run dev
```

The dashboard reads `NEXT_PUBLIC_API_URL` and defaults to `http://localhost:8080`.

To use another backend address:

```bash
NEXT_PUBLIC_API_URL=http://localhost:9090 npm run dev
```

This variable is exposed to the browser. Never place passwords, database URLs, API keys, or secrets in it.

### Type-check

```bash
cd frontend
npm run lint
```

For this prototype, `lint` runs strict TypeScript checking without emitting files.

### Production build test

```bash
cd frontend
npm run build
```

The build must finish without TypeScript or bundling errors. Docker uses the resulting standalone Next.js output.

### Manual UI acceptance check

With both services running:

1. open `http://localhost:3000`;
2. confirm all three algorithm cards appear;
3. click each card and confirm the Gantt heading changes;
4. confirm runtime appears for every algorithm;
5. confirm the validator reports success or a specific violation;
6. click **Run benchmark** and confirm values refresh;
7. resize below 900 px and confirm cards stack vertically;
8. check the browser console for errors.

There is no automated browser suite yet. Add Playwright when the UI gains more interactive workflows.

## 9. Backend and optimizer tests

Run the main repository test target:

```bash
make test
```

It performs a CMake build, the CTest optimizer smoke benchmark, and Go HTTP tests.

Run components separately when debugging:

```bash
ctest --test-dir build --output-on-failure
```

```bash
cd backend
go test ./...
```

```bash
cd frontend
npm run lint
npm run build
```

## 10. Benchmark files

```bash
make benchmark
```

Generated outputs:

```text
benchmark-results/latest.json
benchmark-results/latest.csv
```

`benchmark-results/` is ignored by Git. Override the solver limit for this pipeline with:

```bash
SOLVER_TIME_LIMIT_SECONDS=30 make benchmark
```

## 11. Native OR-Tools CP-SAT

Native CP-SAT is the default. The automated path is:

```bash
make setup-ortools
make build-optimizer
make verify-native
```

The dependency is installed locally at `.deps/or-tools`. CMake receives that location through `ortools_ROOT`; the machine does not need a global OR-Tools installation.

The manual equivalent is:

```bash
cmake -S . -B build -DSIH_WITH_ORTOOLS=ON \
  -Dortools_ROOT="$PWD/.deps/or-tools" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

The CP-SAT plan must report `"native_cp_sat": true`. See `optimizer/ORTOOLS.md` and `docs/NATIVE_CPSAT_TEAM_SETUP.md`.

For a dependency-free diagnostic build only:

```bash
make build-portable
```

## 12. Environment variables

| Variable | Default | Purpose |
|---|---|---|
| `DATABASE_URL` | unset | Enables PostgreSQL benchmark persistence |
| `API_ADDR` | `:8080` | Go listen address |
| `OPTIMIZER_BIN` | `../build/optimizer/sih-optimizer` from `backend/` | C++ executable |
| `DATA_DIR` | `../data/demo` from `backend/` | Input CSV directory |
| `OPTIMIZER_CONFIG` | `../config/optimizer.conf` from `backend/` | Objective configuration |
| `NEXT_PUBLIC_API_URL` | `http://localhost:8080` | Browser-visible API base URL |
| `SOLVER_TIME_LIMIT_SECONDS` | `10` in benchmark script | CP-SAT command time limit |
| `ORTOOLS_ROOT` | `.deps/or-tools` | Local OR-Tools C++ distribution |
| `ORTOOLS_VERSION` | `9.12` | Version pinned by the setup script |
| `CMAKE_BUILD_PARALLEL_LEVEL` | `4` | Parallel C++ build jobs |

Copy `.env.example` to `.env`. The Makefile and development scripts load it automatically; `.env` is ignored by Git and must not be committed.

## 13. Troubleshooting

### Optimizer executable not found

```bash
make build-optimizer
```

If Go is started outside `make api`, check that `OPTIMIZER_BIN`, `DATA_DIR`, and `OPTIMIZER_CONFIG` are correct relative to the current directory.

### Dashboard reports that the API is offline

```bash
curl http://localhost:8080/api/health
```

If Go uses another port, restart the frontend with the matching `NEXT_PUBLIC_API_URL`. Production frontend builds embed this value and must be rebuilt after it changes.

### Port already in use

Stop the conflicting process or change the mapping. When changing the API port, update `NEXT_PUBLIC_API_URL`. When changing PostgreSQL's host port, update `DATABASE_URL`.

### PostgreSQL connects but tables are missing

Compose initialization runs only for a new empty volume. Apply `db/migrations/001_init.sql` manually or intentionally reset the development volume.

### PostgreSQL warning appears but the API starts

This is expected fail-open behavior. Planning still works. Correct `DATABASE_URL` and restart when persistence is required.

### Edited CSV data does not appear

Each optimizer invocation reloads the dataset. Click **Run benchmark** or reload the page, and verify `DATA_DIR` points to the directory you edited.

### CP-SAT still reports `native_cp_sat: false`

The API is using the default binary. Rebuild with `SIH_WITH_ORTOOLS=ON` and point `OPTIMIZER_BIN` at `build-ortools/optimizer/sih-optimizer`.
