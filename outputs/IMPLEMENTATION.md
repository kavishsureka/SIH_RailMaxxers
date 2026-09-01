# RailBlock SIH 26027 implementation handoff

The runnable source repository is the parent workspace directory. Start with `README.md` there.

Implemented:

- C++20 optimizer CLI with independent, coordinated greedy, native OR-Tools CP-SAT build path, portable fallback, shared block builder, weighted objective, and independent validator;
- deterministic five-corridor / thirty-task / fifty-movement all-electric dataset generator;
- benchmark JSON and CSV pipeline with mandatory runtime for every algorithm;
- Go REST API with dataset, plan, benchmark, and health endpoints;
- PostgreSQL migration and optional benchmark persistence;
- Next.js comparison dashboard, weekly Gantt, KPI cards, and validator status;
- local Make targets, Dockerfiles, and Docker Compose stack.

Verified locally:

- C++ build and CTest smoke test;
- Go unit tests and live API calls;
- Next.js production build;
- live dashboard rendering and algorithm switching with no browser console errors.

Native OR-Tools compilation requires a local OR-Tools CMake package and `SIH_WITH_ORTOOLS=ON`. The normal build stays runnable without it and labels the result as `native_cp_sat: false`.
