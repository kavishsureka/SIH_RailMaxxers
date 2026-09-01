# RailBlock — SIH 26027 prototype

RailBlock is a seven-day railway maintenance block planner for Engineering, S&T, and TRD. It compares an independent departmental baseline, a coordinated greedy scheduler, and an OR-Tools CP-SAT model through one independent validator and one KPI contract.

The prototype is deliberately small and explainable: 15-minute slots, five corridors, thirty tasks, fifty synthetic train movements, and no production-only integration layer. Every train is electric; there is no traction or diesel field anywhere in the data contract or database schema.

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
  ├─ OR-Tools CP-SAT (portable fallback when OR-Tools is absent)
  └─ independent shared validator
```

The C++ CLI is the source of truth for plan creation, block derivation, validation, objective calculation, metrics, and mandatory runtime measurement. The Go layer does not reimplement scheduling rules.

## Quick start

Requirements: CMake 3.20+, a C++20 compiler, Go 1.23+, Node 22+, and npm.

```bash
make generate
make build-optimizer
make test
```

Start the API and frontend in separate terminals:

```bash
make api
make web
```

Open [http://localhost:3000](http://localhost:3000). PostgreSQL is optional for local development; if `DATABASE_URL` is absent, the API remains fully usable without persistence.

Detailed teammate documentation:

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
python3 tools/generate_demo.py --seed 26027 --output data/demo
```

## REST API

- `GET /api/health` — service and horizon contract
- `GET /api/dataset` — current synthetic scenario
- `GET|POST /api/benchmark` — run all algorithms and optionally persist the result
- `GET /api/plans/{independent|greedy|cp-sat}` — run one algorithm

## Planning model

Hard rules checked by the shared validator:

1. one continuous placement with the configured duration;
2. placement inside task and corridor availability windows;
3. no overlap with a protected (`HARD`) train movement;
4. power-block work cannot overlap any train movement because all trains are electric;
5. incompatible work types cannot overlap on the same corridor;
6. task dependencies and minimum lag are respected;
7. mandatory work is scheduled.

The configurable weighted objective in `config/optimizer.conf` is:

```text
minimize(wB*block_count
       + wD*downtime_minutes
       + wT*train_impact
       + wO*overdue_penalty
       + wC*critical_noncompletion_penalty)
```

`wC` is dominant by default. Objective weights and runtimes are recorded with benchmark outputs. Contiguous active corridor slots are consolidated into shared blocks, so simultaneous compatible work creates one period of infrastructure downtime rather than the sum of departmental task durations.

## Repository map

```text
backend/              Go REST API and PostgreSQL repository
config/               objective, priority, and train-impact policy
data/demo/            deterministic hackathon dataset
db/migrations/        minimal internal-round PostgreSQL schema
frontend/             Next.js comparison dashboard and weekly Gantt
optimizer/            C++ schedulers, objective, block builder, validator
scripts/              benchmark pipeline
tools/                dataset generator and report conversion
```

## CP-SAT availability

The full OR-Tools model is compiled with `SIH_WITH_ORTOOLS=ON`. The default build uses a deterministic coordinated fallback because OR-Tools C++ is not normally available on a fresh machine. Results explicitly expose `native_cp_sat`, preventing the fallback from being presented as native CP-SAT. See `optimizer/ORTOOLS.md` for the native build command.

## Intentional prototype limits

No authentication, Kafka, Redis, GIS, workforce/equipment capacity, live railway integrations, or real-time re-optimization. Defects are represented as maintenance tasks. The included policy weights and synthetic movements are demonstration assumptions, not railway operating standards.
