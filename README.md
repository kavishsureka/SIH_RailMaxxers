# RailBlock — SIH 26027 prototype

RailBlock is a 28-day railway maintenance block planner for Engineering, S&T, and TRD. It compares an independent departmental baseline, a coordinated greedy scheduler, and a full-horizon OR-Tools CP-SAT model through one independent validator and one KPI contract.

The live-demo scenario contains 10 corridors, 120 monthly tasks, and 120 synthetic train movements per day across four weeks. Every train is electric; there is no traction or diesel field anywhere in the data contract or database schema. Generated 100-, 250-, and 500-task benchmark presets are available under `data/benchmarks/`.

## Architecture

```text
Next.js dashboard
        │ REST
        ▼
Go API ─────────────── PostgreSQL
        │ process/JSON
        ▼
C++ optimizer
  ├─ independent departmental baseline
  ├─ coordinated greedy
  ├─ native OR-Tools CP-SAT
  └─ independent shared validator
```

The C++ CLI is the source of truth for plan creation, block derivation, validation, objective calculation, metrics, and mandatory runtime measurement. The Go layer does not reimplement scheduling rules.

## Quick start

Requirements: CMake 3.20+, a C++20 compiler, Go 1.23+, Node 22+, and npm.

One-time setup (downloads OR-Tools locally, installs dependencies, builds, and verifies native CP-SAT):

```bash
cp .env.example .env
make setup
```

Daily development:

```bash
make dev
```

Open [http://localhost:3000](http://localhost:3000). PostgreSQL is optional for local development; if `DATABASE_URL` is absent, the API remains fully usable without persistence.

Detailed teammate documentation:

- [`docs/NATIVE_CPSAT_TEAM_SETUP.md`](docs/NATIVE_CPSAT_TEAM_SETUP.md) — native CP-SAT installation, personal `.env`, short run commands, and verification.
- [`docs/SETUP_AND_RUNNING.md`](docs/SETUP_AND_RUNNING.md) — prerequisites, local and Docker setup, PostgreSQL and database-free modes, testing, native OR-Tools, and troubleshooting.
- [`docs/REPOSITORY_GUIDE.md`](docs/REPOSITORY_GUIDE.md) — folder ownership, file-by-file navigation, architecture boundaries, and runtime data flows.

For the full container stack:

```bash
docker compose up --build
```

## Core commands

```bash
# Three-algorithm JSON benchmark
./build/optimizer/sih-optimizer benchmark --data data/demo --config config/optimizer.conf

# JSON + flat CSV benchmark artifacts
make benchmark

# Regenerate a deterministic scenario
make generate

# Regenerate 100/250/500-task benchmark presets
make generate-presets
```

## REST API

- `GET /api/health` — service and horizon contract
- `GET /api/dataset` — current synthetic scenario
- `GET|POST /api/benchmark` — run all algorithms and optionally persist the result
- `GET /api/plans/{independent|greedy|cp-sat}` — run one algorithm

## Planning model

Hard rules checked by the shared validator:

1. every monthly task is scheduled exactly once in the 28-day horizon;
2. one continuous placement with the configured duration;
3. placement inside task and corridor availability windows;
4. no overlap with a protected (`HARD`) train movement;
5. mandatory/critical work finishes within its tighter due-date window;
6. incompatible work types cannot overlap on the same corridor;
7. task dependencies and minimum lag are respected.

Candidate preprocessing intersects task windows with corridor availability, sorts and merges only `HARD` forbidden train intervals, computes free intervals, and removes intervals shorter than the task duration. `SOFT` train movements stay feasible and add train-impact cost when a selected block overlaps them. Power-block tasks follow the same HARD/SOFT contract; the all-electric assumption removes traction-specific exceptions, not SOFT feasibility.

The configurable weighted objective in `config/optimizer.conf` is:

```text
minimize(wB*block_count
       + wD*downtime_minutes
       + wT*train_impact
       + wL*lateness_minutes
       + wV*deadline_violations)
```

There is no unscheduled-task term because unscheduling is infeasible. Objective weights and `preprocessing_ms`, `algorithm_ms`, and `total_runtime_ms` are recorded for every algorithm. Contiguous active corridor slots are consolidated into shared blocks, so simultaneous compatible work creates one period of infrastructure downtime rather than the sum of departmental task durations.

## Repository map

```text
backend/              Go REST API and PostgreSQL repository
config/               objective, priority, and train-impact policy
data/demo/            deterministic hackathon dataset
db/migrations/        minimal internal-round PostgreSQL schema
frontend/             Next.js comparison dashboard and four-week Gantt
optimizer/            C++ schedulers, objective, block builder, validator
scripts/              benchmark pipeline
tools/                dataset generator and report conversion
```

## CP-SAT availability

The normal build compiles the full OR-Tools model with `SIH_WITH_ORTOOLS=ON`. `make setup-ortools` installs the pinned C++ binary distribution under the ignored `.deps/` directory, and every result exposes `native_cp_sat` so reports cannot misrepresent the engine. `make build-portable` is an explicit fallback for troubleshooting only. See `optimizer/ORTOOLS.md` for details.

## Intentional prototype limits

No authentication, Kafka, Redis, GIS, workforce/equipment capacity, live railway integrations, or real-time re-optimization. Defects are represented as maintenance tasks. The included policy weights and synthetic movements are demonstration assumptions, not railway operating standards.
