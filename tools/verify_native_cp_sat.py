#!/usr/bin/env python3
"""Run the CP-SAT path and fail unless the result came from native OR-Tools."""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--time-limit", type=int, default=10)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--project-root", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--metadata", required=True)
    args = parser.parse_args()

    priority_file = tempfile.NamedTemporaryFile(suffix=".csv", delete=False)
    priority_file.close()
    inference = subprocess.run(
        [args.python, "-m", "ml.src.inference", "--tasks", str(Path(args.data) / "tasks.csv"),
         "--model", args.model, "--metadata", args.metadata, "--output-csv", priority_file.name],
        cwd=args.project_root, check=False, capture_output=True, text=True,
    )
    if inference.returncode != 0:
        print(f"ERROR: ML priority inference failed: {inference.stderr}", file=sys.stderr)
        Path(priority_file.name).unlink(missing_ok=True)
        return 1

    completed = subprocess.run(
        [
            args.binary,
            "cp-sat",
            "--data",
            args.data,
            "--priorities",
            priority_file.name,
            "--config",
            args.config,
            "--time-limit",
            str(args.time_limit),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    Path(priority_file.name).unlink(missing_ok=True)
    if completed.returncode != 0:
        print(f"ERROR: native optimizer exited with code {completed.returncode}.", file=sys.stderr)
        if completed.stderr:
            print(completed.stderr.rstrip(), file=sys.stderr)
        return 1

    result = json.loads(completed.stdout)
    if result.get("native_cp_sat") is not True:
        print("ERROR: optimizer used the portable fallback, not native CP-SAT.", file=sys.stderr)
        return 1
    if not result.get("validation", {}).get("valid", False):
        violations = result.get("validation", {}).get("violations", [])
        print(f"ERROR: validator rejected the CP-SAT plan: {violations}", file=sys.stderr)
        return 1

    print(
        "Native CP-SAT verified: "
        f"status={result['solver_status']}, "
        f"total_runtime_ms={result['total_runtime_ms']}, "
        f"objective={result['metrics']['objective']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
