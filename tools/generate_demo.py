#!/usr/bin/env python3
"""Generate deterministic all-electric 28-day SIH 26027 datasets."""
from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path

SLOTS_PER_DAY = 96
HORIZON_DAYS = 28
HORIZON_SLOTS = HORIZON_DAYS * SLOTS_PER_DAY


def write(path: Path, header: list[str], rows: list[list[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data/demo")
    parser.add_argument("--seed", type=int, default=26027)
    parser.add_argument("--corridors", type=int, default=10)
    parser.add_argument("--tasks", type=int, default=120)
    parser.add_argument("--trains-per-day", type=int, default=120)
    parser.add_argument("--profile", choices=["alpha", "beta", "gamma"], default="alpha")
    parser.add_argument("--trains", type=int, default=None, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.trains is not None:
        args.trains_per_day = max(1, round(args.trains / HORIZON_DAYS))
    if not 1 <= args.corridors <= 10:
        parser.error("--corridors must be between 1 and 10")

    rng = random.Random(args.seed)
    out = Path(args.output)
    corridor_names = [
        "Delhi-Ghaziabad", "Mumbai-Kalyan", "Howrah-Barddhaman", "Chennai-Arakkonam",
        "Pune-Lonavala", "Ahmedabad-Vadodara", "Lucknow-Kanpur", "Jaipur-Bandikui",
        "Bengaluru-Whitefield", "Secunderabad-Kazipet",
    ]
    corridors = [[f"C{i + 1}", corridor_names[i]] for i in range(args.corridors)]
    write(out / "corridors.csv", ["id", "name"], corridors)

    department_cycles = {
        "alpha": ["ENGINEERING", "ENGINEERING", "ST", "TRD"],
        "beta": ["ST", "TRD", "ST", "TRD", "ENGINEERING"],
        "gamma": ["ENGINEERING", "ST", "TRD"],
    }
    departments = department_cycles[args.profile]
    task_types = {
        "ENGINEERING": ["TRACK_REPAIR", "RAIL_GRINDING", "HEAVY_EARTHWORK"],
        "ST": ["SIGNAL_TEST", "TRACK_CIRCUIT_CALIBRATION", "POINT_MACHINE_SERVICE"],
        "TRD": ["OHE_INSPECTION", "OHE_POWER_WORK", "SUBSTATION_TEST"],
    }
    critical_count = min(args.tasks, max(12, round(args.tasks * 0.10)))
    high_count = min(args.tasks - critical_count, max(10, round(args.tasks * 0.10)))
    power_ratio = {"alpha": 0.12, "beta": 0.22, "gamma": 0.25}[args.profile]
    power_remaining = max(1, round(args.tasks * power_ratio))
    tasks: list[list[object]] = []
    task_meta: list[dict[str, int | str]] = []
    for i in range(args.tasks):
        corridor_index = i % len(corridors)
        round_index = i // len(corridors)
        department = departments[round_index % len(departments)]
        type_index = (round_index // len(departments)) % 3
        task_type = task_types[department][type_index]
        if department == "TRD":
            if power_remaining > 0:
                task_type = "OHE_POWER_WORK" if power_remaining % 2 else "SUBSTATION_TEST"
                power_remaining -= 1
            else:
                task_type = "OHE_INSPECTION"
        requires_power = department == "TRD" and task_type in {"OHE_POWER_WORK", "SUBSTATION_TEST"}

        duration = rng.choice([2, 4, 6, 8, 12])
        week = round_index % 4
        day_in_week = (corridor_index + round_index) % 6
        earliest = week * 7 * SLOTS_PER_DAY + day_in_week * SLOTS_PER_DAY
        latest = min(HORIZON_SLOTS, earliest + rng.choice([5, 6, 7]) * SLOTS_PER_DAY)

        if i < critical_count:
            severity, criticality = 9, rng.choice([8, 9, 10])
            mandatory = i < min(8, critical_count)
            due = min(latest, earliest + rng.choice([4, 5]) * SLOTS_PER_DAY)
        elif i < critical_count + high_count:
            severity, criticality = 8, 8
            mandatory = False
            due = min(HORIZON_SLOTS, earliest + rng.choice([3, 4, 5]) * SLOTS_PER_DAY)
        else:
            severity, criticality = rng.randint(4, 7), rng.randint(4, 8)
            mandatory = False
            due = min(HORIZON_SLOTS, earliest + rng.choice([2, 4, 6, 8]) * SLOTS_PER_DAY)

        task_id = f"T{i + 1:03}"
        tasks.append([
            task_id, corridors[corridor_index][0], department, task_type, duration, severity,
            criticality, due, str(mandatory).lower(), str(requires_power).lower(), earliest, latest,
        ])
        task_meta.append({"id": task_id})
    write(out / "tasks.csv", [
        "id", "corridor_id", "department", "task_type", "duration_slots", "severity",
        "criticality", "due_slot", "mandatory", "requires_power_block", "earliest_slot",
        "latest_end_slot",
    ], tasks)

    # Every movement is electric by domain assumption; there is deliberately no traction column.
    trains: list[list[object]] = []
    train_id = 1
    for day in range(HORIZON_DAYS):
        for daily_index in range(args.trains_per_day):
            corridor = corridors[daily_index % len(corridors)][0]
            sequence = daily_index // len(corridors)
            nominal = (sequence * 8 + (daily_index % len(corridors)) * 3) % SLOTS_PER_DAY
            start = day * SLOTS_PER_DAY + min(SLOTS_PER_DAY - 2, nominal + rng.choice([0, 0, 1]))
            mode = "HARD" if (daily_index + day) % 5 == 0 else "SOFT"
            impact = 10 if mode == "HARD" else rng.choice([1, 3, 5, 7])
            trains.append([
                f"TR{train_id:05}", corridor, start, min(start + rng.choice([1, 2]), HORIZON_SLOTS),
                mode, impact,
            ])
            train_id += 1
    write(out / "trains.csv", [
        "id", "corridor_id", "start_slot", "end_slot", "conflict_mode", "impact_weight",
    ], trains)

    availability: list[list[object]] = []
    for corridor, _ in corridors:
        for day in range(HORIZON_DAYS):
            base = day * SLOTS_PER_DAY
            availability.extend([
                [corridor, base, base + 24], [corridor, base + 40, base + 68],
                [corridor, base + 82, base + SLOTS_PER_DAY],
            ])
    write(out / "availability.csv", ["corridor_id", "start_slot", "end_slot"], availability)

    dependency_ratio = {"alpha": 0.08, "beta": 0.10, "gamma": 0.15}[args.profile]
    dependency_target = round(args.tasks * dependency_ratio) if args.tasks >= 20 else 0
    dependencies: list[list[object]] = []
    for base_round in range(0, max(0, args.tasks // len(corridors) - 1)):
        if base_round % 4 == 3:
            continue
        for corridor_index in range(len(corridors)):
            predecessor_index = base_round * len(corridors) + corridor_index
            successor_index = (base_round + 1) * len(corridors) + corridor_index
            if successor_index >= args.tasks or len(dependencies) >= dependency_target:
                break
            dependencies.append([
                task_meta[predecessor_index]["id"], task_meta[successor_index]["id"],
                rng.choice([0, 1, 2]),
            ])
        if len(dependencies) >= dependency_target:
            break
    write(out / "dependencies.csv", [
        "predecessor_task_id", "successor_task_id", "min_lag_slots",
    ], dependencies)

    compatibility = [
        ["HEAVY_EARTHWORK", "OHE_POWER_WORK", "false"],
        ["TRACK_REPAIR", "SIGNAL_TEST", "false"],
        ["RAIL_GRINDING", "TRACK_CIRCUIT_CALIBRATION", "false"],
        ["OHE_INSPECTION", "SIGNAL_TEST", "true"],
    ]
    write(out / "compatibility.csv", ["task_type_a", "task_type_b", "compatible"], compatibility)
    print(
        f"generated 28-day dataset: {len(corridors)} corridors, {len(tasks)} tasks, "
        f"{len(trains)} all-electric movements ({args.trains_per_day}/day), "
        f"{len(dependencies)} dependencies, {args.profile} profile in {out}"
    )


if __name__ == "__main__":
    main()
