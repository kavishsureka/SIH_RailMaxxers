#!/usr/bin/env python3
"""Generate a deterministic, all-electric SIH 26027 demo dataset."""
from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path


def write(path: Path, header: list[str], rows: list[list[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data/demo")
    parser.add_argument("--seed", type=int, default=26027)
    parser.add_argument("--corridors", type=int, default=5)
    parser.add_argument("--tasks", type=int, default=30)
    parser.add_argument("--trains", type=int, default=50)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    out = Path(args.output)

    corridors = [[f"C{i+1}", name] for i, name in enumerate(
        ["Delhi-Ghaziabad", "Mumbai-Kalyan", "Howrah-Barddhaman", "Chennai-Arakkonam", "Pune-Lonavala"][:args.corridors])]
    write(out / "corridors.csv", ["id", "name"], corridors)

    departments = ["ENGINEERING", "ST", "TRD"]
    types = {
        "ENGINEERING": ["TRACK_REPAIR", "RAIL_GRINDING", "HEAVY_EARTHWORK"],
        "ST": ["SIGNAL_TEST", "TRACK_CIRCUIT_CALIBRATION", "POINT_MACHINE_SERVICE"],
        "TRD": ["OHE_INSPECTION", "OHE_POWER_WORK", "SUBSTATION_TEST"],
    }
    tasks: list[list[object]] = []
    for i in range(args.tasks):
        dept = departments[i % 3]
        task_type = types[dept][(i // 3) % 3]
        corridor = corridors[i % len(corridors)][0]
        duration = rng.choice([2, 4, 6, 8, 12])
        earliest = (i % 7) * 96 + rng.choice([0, 4, 8, 12])
        latest = min(672, earliest + rng.choice([36, 48, 64, 80]))
        severity = 10 if i in (0, 7) else rng.randint(4, 9)
        criticality = 9 if i in (0, 7, 14) else rng.randint(4, 9)
        due = rng.choice([480, 576, 672, 768])
        mandatory = i in (0, 7, 14)
        power = dept == "TRD" and task_type in ("OHE_POWER_WORK", "SUBSTATION_TEST")
        tasks.append([f"T{i+1:02}", corridor, dept, task_type, duration, severity,
                      criticality, due, str(mandatory).lower(), str(power).lower(), earliest, latest])
    write(out / "tasks.csv", ["id", "corridor_id", "department", "task_type", "duration_slots",
        "severity", "criticality", "due_slot", "mandatory", "requires_power_block", "earliest_slot", "latest_end_slot"], tasks)

    # Every movement is electric by domain assumption, so there is deliberately no traction column.
    trains: list[list[object]] = []
    for i in range(args.trains):
        corridor = corridors[i % len(corridors)][0]
        day = i % 7
        start = day * 96 + rng.choice([24, 28, 32, 36, 40, 68, 72, 76, 80])
        category = i % 5
        mode = "HARD" if category == 0 else "SOFT"
        impact = 10 if category == 0 else (5 if category in (1, 2, 3) else 1)
        trains.append([f"TR{i+1:03}", corridor, start, min(start + rng.choice([1, 2]), 672), mode, impact])
    write(out / "trains.csv", ["id", "corridor_id", "start_slot", "end_slot", "conflict_mode", "impact_weight"], trains)

    availability: list[list[object]] = []
    for corridor, _ in corridors:
        for day in range(7):
            base = day * 96
            availability.extend([[corridor, base, base + 24], [corridor, base + 44, base + 68], [corridor, base + 84, base + 96]])
    write(out / "availability.csv", ["corridor_id", "start_slot", "end_slot"], availability)

    dependencies = [["T01", "T02", 1], ["T10", "T11", 0], ["T19", "T20", 1]]
    write(out / "dependencies.csv", ["predecessor_task_id", "successor_task_id", "min_lag_slots"], dependencies)
    compatibility = [
        ["HEAVY_EARTHWORK", "OHE_POWER_WORK", "false"],
        ["TRACK_REPAIR", "SIGNAL_TEST", "false"],
        ["RAIL_GRINDING", "TRACK_CIRCUIT_CALIBRATION", "false"],
        ["OHE_INSPECTION", "SIGNAL_TEST", "true"],
    ]
    write(out / "compatibility.csv", ["task_type_a", "task_type_b", "compatible"], compatibility)
    print(f"generated {len(corridors)} corridors, {len(tasks)} tasks, {len(trains)} all-electric movements in {out}")


if __name__ == "__main__":
    main()
