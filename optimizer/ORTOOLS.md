# Native OR-Tools CP-SAT build

Native CP-SAT is the default development path. From the repository root, run:

```bash
cp .env.example .env
make setup
```

The setup script downloads the pinned OR-Tools C++ distribution into `.deps/or-tools`, which is ignored by Git. The native CMake build is equivalent to:

```bash
cmake -S . -B build -DSIH_WITH_ORTOOLS=ON \
  -Dortools_ROOT="$PWD/.deps/or-tools" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

Run `make verify-native` at any time. It first performs batch inference with the persisted ML priority model, passes the resulting score file through required `--priorities`, and fails unless the JSON result contains both `"native_cp_sat": true` and a valid independent-validator result.

The CLI and JSON contract are identical across builds. For troubleshooting only, `make build-portable` creates a separate `build-portable/` binary whose local-search fallback reports `"native_cp_sat": false`.

For supported archives and teammate setup, see [`../docs/NATIVE_CPSAT_TEAM_SETUP.md`](../docs/NATIVE_CPSAT_TEAM_SETUP.md).

The native model uses Boolean candidate-start variables on the 2,688-slot monthly grid, shared corridor-active and block-start variables, compulsory exactly-once scheduling, compatibility, due-date, and dependency constraints. Availability and HARD-train exclusion are enforced by the shared candidate domain before model construction. Candidate starts are sampled every two slots (30 minutes), with the latest legal start appended. SOFT movements remain in the domain and are costed. CP-SAT uses eight workers, random seed `26027`, and the CLI/API time limit. Its single configurable integer objective is:

`wB*block_count + wD*downtime_minutes + wT*train_impact + wL*lateness_minutes + wV*deadline_violations + wP*priority_weighted_delay_score_days`.

Every result is finalized through the same block builder, independent placement validator, metric calculator, task-trace builder, and runtime measurement as Independent and Greedy. Raw JSON reports `preprocessing_ms`, `algorithm_ms`, and `total_runtime_ms`.

For model variables, constraints, objective coefficients, timing boundaries, and the fallback algorithm, see [`../docs/SOLVER_AND_OPTIMIZATION.md`](../docs/SOLVER_AND_OPTIMIZATION.md). For priority generation and provenance, see [`../docs/ML_PRIORITY_MODEL.md`](../docs/ML_PRIORITY_MODEL.md).
