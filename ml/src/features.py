"""Stable task-to-model feature extraction boundary."""

from __future__ import annotations

import csv
from pathlib import Path
from typing import Iterable, Mapping

import numpy as np

FEATURE_NAMES = [
    "severity",
    "asset_criticality",
    "overdue_factor",
    "deadline_urgency",
]
HORIZON_SLOTS = 28 * 96


def _clip(value: float) -> float:
    return min(1.0, max(0.0, value))


def extract_task_features(task: Mapping[str, str | int | float]) -> list[float]:
    """Extract four normalized policy inputs without leaking optimizer rules.

    Current scenario CSVs have no maintenance-age/overdue column. For those
    non-overdue tasks the factor is zero. A future railway feed can provide an
    explicit normalized ``overdue_factor`` without changing callers.
    """
    due_slot = float(task.get("due_slot", HORIZON_SLOTS))
    explicit_overdue = task.get("overdue_factor")
    criticality = task.get("asset_criticality")
    if criticality in (None, ""):
        criticality = task["criticality"]
    overdue = (
        float(explicit_overdue)
        if explicit_overdue not in (None, "")
        else max(0.0, -due_slot / HORIZON_SLOTS)
    )
    deadline_urgency = _clip(1.0 - max(0.0, due_slot) / HORIZON_SLOTS)
    return [
        _clip(float(task["severity"]) / 10.0),
        _clip(float(criticality) / 10.0),
        _clip(overdue),
        deadline_urgency,
    ]


def feature_matrix(tasks: Iterable[Mapping[str, str | int | float]]) -> np.ndarray:
    rows = [extract_task_features(task) for task in tasks]
    return np.asarray(rows, dtype=np.float64).reshape((-1, len(FEATURE_NAMES)))


def read_tasks(path: str | Path) -> list[dict[str, str]]:
    with Path(path).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))
