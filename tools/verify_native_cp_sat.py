#!/usr/bin/env python3
"""Run the CP-SAT path and fail unless the result came from native OR-Tools."""

import argparse
import json
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--time-limit", type=int, default=10)
    args = parser.parse_args()

    completed = subprocess.run(
        [
            args.binary,
            "cp-sat",
            "--data",
            args.data,
            "--config",
            args.config,
            "--time-limit",
            str(args.time_limit),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
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
        f"runtime_ms={result['runtime_ms']}, "
        f"objective={result['metrics']['objective']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
