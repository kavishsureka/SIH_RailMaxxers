#!/usr/bin/env python3
import csv
import json
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
payload = json.loads(source.read_text())
fields = ["algorithm", "solver_status", "native_cp_sat", "preprocessing_ms", "algorithm_ms",
          "total_runtime_ms", "valid", "violations", "objective", "block_count",
          "downtime_minutes", "train_impact", "lateness_minutes", "deadline_violations",
          "scheduled_tasks", "total_tasks", "critical_completed", "critical_total"]
with destination.open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    for plan in payload["plans"]:
        row = {key: plan.get(key) for key in fields}
        row.update(plan["metrics"])
        row["valid"] = plan["validation"]["valid"]
        row["violations"] = len(plan["validation"]["violations"])
        writer.writerow(row)
