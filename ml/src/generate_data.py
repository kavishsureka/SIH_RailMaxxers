"""Deterministic, realistic-ish synthetic feature generator."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np

from .features import FEATURE_NAMES
from .labels import bootstrap_priority_label


def generate_synthetic_data(sample_count: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    # Severity is mostly routine/moderate with a deliberate critical tail.
    severity = rng.beta(2.2, 3.4, sample_count)
    critical_tail = rng.random(sample_count) < 0.12
    severity[critical_tail] = rng.beta(8.0, 1.8, critical_tail.sum())
    # Criticality is correlated with severity but retains independent variation.
    asset_criticality = np.clip(0.48 * severity + 0.52 * rng.beta(2.6, 2.2, sample_count), 0, 1)
    # Most work is not overdue; overdue work has a right-skewed age distribution.
    overdue_factor = np.zeros(sample_count)
    overdue_mask = rng.random(sample_count) < 0.18
    overdue_factor[overdue_mask] = rng.beta(1.4, 4.0, overdue_mask.sum())
    # Deadlines cluster around medium urgency with smaller immediate/far-future tails.
    deadline_urgency = rng.beta(2.0, 2.8, sample_count)
    urgent_mask = rng.random(sample_count) < 0.10
    deadline_urgency[urgent_mask] = rng.beta(7.0, 1.7, urgent_mask.sum())
    features = np.column_stack((severity, asset_criticality, overdue_factor, deadline_urgency))
    labels = bootstrap_priority_label(severity, asset_criticality, overdue_factor, deadline_urgency)
    return features, labels


def write_dataset(path: Path, features: np.ndarray, labels: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow([*FEATURE_NAMES, "priority_score"])
        writer.writerows([*row, label] for row, label in zip(features, labels))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=50000)
    parser.add_argument("--seed", type=int, default=26027)
    args = parser.parse_args()
    features, labels = generate_synthetic_data(args.samples, args.seed)
    write_dataset(args.output, features, labels)


if __name__ == "__main__":
    main()

