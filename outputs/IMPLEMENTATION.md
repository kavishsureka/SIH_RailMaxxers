# RailBlock SIH 26027 implementation handoff

The current implementation is a Next.js 16 frontend, Go 1.23 API, C++20 optimizer with native OR-Tools CP-SAT support, and optional PostgreSQL 17 benchmark persistence.

Implemented and represented in the canonical documentation:

- one 28-day/2,688-slot horizon with every monthly task required exactly once;
- deterministic medium Alpha (110 tasks), Beta (124), and Gamma (120) scenarios selected by request `dataset_id`;
- all-electric HARD/SOFT train model with no traction field;
- shared candidate preprocessing, Independent baseline, coordinated Greedy, and full-month CP-SAT;
- one weighted objective (`wB`, `wD`, `wT`, `wL`, `wV`), shared independent validator, derived blocks/traces, and all three runtime fields;
- Overview, Block Planner, Maintenance Tasks, Plan Verification, Benchmark, task drawer, and block drawer;
- optional write of benchmark JSON to `benchmark_runs`; normalized schema exists but is not the current optimizer input.

Canonical guides:

- [`../README.md`](../README.md)
- [`../docs/IMPLEMENTATION_GUIDE.md`](../docs/IMPLEMENTATION_GUIDE.md)
- [`../docs/REPOSITORY_GUIDE.md`](../docs/REPOSITORY_GUIDE.md)
- [`../docs/SETUP_AND_RUNNING.md`](../docs/SETUP_AND_RUNNING.md)

Native local verification is `make verify-native`. The Docker API uses the explicitly labelled portable fallback and must not be presented as native CP-SAT.
