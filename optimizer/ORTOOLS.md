# Native OR-Tools CP-SAT build

The portable build keeps the demo runnable without external native packages. For the real CP-SAT implementation, install OR-Tools with its CMake package metadata and build with:

```bash
cmake -S . -B build-ortools \
  -DSIH_WITH_ORTOOLS=ON \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/ortools
cmake --build build-ortools -j
```

The CLI and JSON contract are identical. A native result returns `"native_cp_sat": true`; the portable local-search fallback returns `false`, so benchmark reports cannot accidentally misrepresent which engine ran.

The native model uses Boolean task-start variables on the 672-slot grid, shared corridor-active and block-start variables, optional task scheduling, compatibility, protected-train, all-electric power-block, availability, and dependency constraints. Its single configurable integer objective is:

`wB*block_count + wD*downtime_minutes + wT*train_impact + wO*overdue_penalty + wC*critical_noncompletion_penalty`.

