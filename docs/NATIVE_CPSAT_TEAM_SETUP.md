# Teammate setup: native CP-SAT and personal environment

This is the shortest reliable path from a fresh clone to the working SIH planner. Native Google OR-Tools CP-SAT is the normal build; the portable fallback is only for troubleshooting.

The native model solves one 2,688-slot (28-day) month and requires every selected-scenario task exactly once. Scenario selection is request-scoped; `DATA_ROOT` is a base directory, not an Alpha/Beta/Gamma switch.

## 1. Install the machine-level prerequisites

Install these once:

- Git;
- CMake 3.20 or newer;
- a C++20 compiler (`clang++` on macOS, GCC/Clang on Linux);
- Go 1.23 or newer;
- Node.js 22 LTS and npm;
- Python 3;
- `curl`, `tar`, and `make`;
- Docker Desktop or Docker Engine only if you want PostgreSQL through Compose.

macOS teammates should install Xcode Command Line Tools first. On Windows, use WSL 2 with Ubuntu 22.04 or 24.04; the local setup script targets Unix-like development environments.

Check the important tools:

```bash
cmake --version
clang++ --version   # or: g++ --version
go version
node --version
npm --version
python3 --version
```

The project uses Google's prebuilt C++ distribution, which is the recommended installation route when OR-Tools itself is not being modified: <https://developers.google.com/optimization/install/cpp>.

## 2. Clone and create your own `.env`

From the repository root:

```bash
cp .env.example .env
```

Every teammate needs their own `.env`. It is ignored by Git and must never be committed. The checked-in `.env.example` is the shared template.

The default file runs without PostgreSQL:

```dotenv
DATABASE_URL=
API_ADDR=:8080
OPTIMIZER_BIN=../build/optimizer/sih-optimizer
DATA_ROOT=../data/scenarios
OPTIMIZER_CONFIG=../config/optimizer.conf
PROJECT_ROOT=..
ML_PYTHON=../work/ml-venv/bin/python
ML_MODEL=../ml/models/priority_gbr_v1.joblib
ML_MODEL_METADATA=../ml/models/priority_gbr_v1.metadata.json
NEXT_PUBLIC_API_URL=http://localhost:8080
ORTOOLS_ROOT=.deps/or-tools
ORTOOLS_VERSION=9.12
SOLVER_TIME_LIMIT_SECONDS=15
CMAKE_BUILD_PARALLEL_LEVEL=4
```

Usually, only these values need changing:

- `DATABASE_URL`: blank means no persistence; set it to use PostgreSQL;
- `ML_PYTHON`, `ML_MODEL`, and `ML_MODEL_METADATA`: runtime priority inference executable/artifacts;
- `SOLVER_TIME_LIMIT_SECONDS`: native CP-SAT time budget used by verification and benchmarks;
- `CMAKE_BUILD_PARALLEL_LEVEL`: lower this if compilation consumes too much memory;
- `API_ADDR` and `NEXT_PUBLIC_API_URL`: change together if port 8080 is unavailable.

Confirm that personal and downloaded files are ignored:

```bash
git status --short
```

Neither `.env` nor `.deps/` should appear.

## 3. Run the one-time setup

```bash
make setup
```

This command:

1. creates `.env` if it is missing;
2. installs frontend and Go dependencies and creates `work/ml-venv` with pinned ML packages;
3. downloads the pinned OR-Tools C++ archive into `.deps/or-tools`;
4. builds the C++ optimizer with `SIH_WITH_ORTOOLS=ON`;
5. runs batch ML inference for Alpha, passes the generated priorities to CP-SAT, and requires `native_cp_sat: true` plus a valid independent-validator result.

The downloaded OR-Tools files stay local and are not added to Git. The setup script currently selects official archives for Apple Silicon/Intel macOS, Ubuntu 20.04/22.04/24.04 x86-64, Debian 11/12 x86-64, and Debian 11 ARM64.

If your Linux distribution is not listed, place the URL of a compatible OR-Tools C++ `.tar.gz` archive in your `.env`:

```dotenv
ORTOOLS_ARCHIVE_URL=https://github.com/google/or-tools/releases/download/v9.12/<compatible-archive>.tar.gz
```

Use an archive published on the official OR-Tools releases page: <https://github.com/google/or-tools/releases>.

## 4. Run the application each day

The shortest command starts the Go API and Next.js dashboard together:

```bash
make dev
```

Open <http://localhost:3000>. Stop both services with `Ctrl+C`.

For separate logs or debugging, use two terminals:

```bash
# Terminal 1
make api

# Terminal 2
make web
```

The API is available at <http://localhost:8080>; `GET /api/health` is the quickest health check.

## 5. Choose database-free or PostgreSQL mode

### Database-free mode (default)

Keep this in `.env`:

```dotenv
DATABASE_URL=
```

Then run `make dev`. Planning, validation, comparison, and the Gantt UI work normally; benchmark history is not persisted.

### PostgreSQL mode

Start the repository's database container:

```bash
make db-up
```

Set this in `.env`:

```dotenv
DATABASE_URL=postgres://railblock:railblock@localhost:5432/railblock?sslmode=disable
```

Then run `make dev`. Stop only PostgreSQL with `make db-down`. Use `make down` to stop the full Compose stack.

## 6. Build, verify, test, and benchmark

```bash
make build-optimizer  # native OR-Tools build
make verify-native   # proves CP-SAT is native and validator-approved
make train-ml        # retrains the persisted priority model when intended
make test-ml         # ML feature/training/inference tests
make test            # ML, C++, Go, and frontend checks
make benchmark       # three algorithms; writes JSON and CSV reports
```

Benchmark files are written under `benchmark-results/` and are ignored by Git. The script uses `scenario-alpha`. Runtime is recorded as preprocessing, algorithm/solver, and total milliseconds for Independent, Greedy, and CP-SAT.

The portable diagnostic build is separate and cannot overwrite the native binary:

```bash
make build-portable
```

Its CP-SAT-shaped fallback reports `native_cp_sat: false`; do not use it for native benchmark claims.

## 7. Common fixes

### `OR-Tools is missing`

Run:

```bash
make setup-ortools
make build-optimizer
```

The expected CMake file is `.deps/or-tools/lib/cmake/ortools/ortoolsConfig.cmake`.

### An interrupted download left an incomplete `.deps/or-tools`

Move that incomplete directory outside the repository, then rerun `make setup-ortools`. The script deliberately does not overwrite an existing directory.

### macOS says an OR-Tools library is disallowed by system policy

First approve the trusted OR-Tools download in macOS Privacy & Security. Then clear the quarantine marker only from this repository's downloaded dependency:

```bash
xattr -dr com.apple.quarantine .deps/or-tools
make verify-native
```

Do not run that command on unrelated downloads or broad directories.

### CMake still remembers a portable build

`make build-optimizer` reconfigures `build/` with native CP-SAT. Check the final verification with `make verify-native`.

### Port 8080 or 3000 is already in use

Stop the conflicting process. For the API, change both `API_ADDR` and `NEXT_PUBLIC_API_URL` in `.env`. For Next.js, run `cd frontend && npm run dev -- --port <port>`.

### Frontend dependencies are missing

Run `make install-deps`. This uses `npm ci`, so dependency versions match the lockfile.

### ML inference fails or Python is missing packages

Run `make install-deps` to recreate/install `work/ml-venv`, then `make test-ml`. Confirm the three `.env` ML paths resolve from `backend/`. The runtime loads the committed model; it does not train on API startup.

### PostgreSQL is unavailable

Clear `DATABASE_URL` and restart `make dev`; the API intentionally supports database-free development.

### Native locally, fallback in Docker

This is expected. `backend/Dockerfile` compiles the dependency-free C++ branch and reports `native_cp_sat: false`. Use `make dev` with `build/optimizer/sih-optimizer` for the native judge/demo path.

## 8. Before opening a pull request

```bash
make verify-native
make test
git status --short
```

Commit source, config, migrations, scripts, and documentation. Do not commit `.env`, `.deps/`, `build/`, `.next/`, `node_modules/`, or benchmark output.
