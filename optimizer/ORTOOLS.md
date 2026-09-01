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

Run `make verify-native` at any time. It executes the model and fails unless the JSON result contains both `"native_cp_sat": true` and a valid independent-validator result.

The CLI and JSON contract are identical across builds. For troubleshooting only, `make build-portable` creates a separate `build-portable/` binary whose local-search fallback reports `"native_cp_sat": false`.

For supported archives and teammate setup, see [`../docs/NATIVE_CPSAT_TEAM_SETUP.md`](../docs/NATIVE_CPSAT_TEAM_SETUP.md).

The native model uses Boolean candidate-start variables on the 2,688-slot monthly grid, shared corridor-active and block-start variables, compulsory task scheduling, compatibility, protected-train, availability, due-date, and dependency constraints. Candidate preprocessing subtracts merged HARD train intervals only; SOFT movements remain in the domain and are costed. Its single configurable integer objective is:

`wB*block_count + wD*downtime_minutes + wT*train_impact + wL*lateness_minutes + wV*deadline_violations`.
