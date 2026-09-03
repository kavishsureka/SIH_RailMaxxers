"""Bootstrap supervision for the prototype priority model.

This policy is used only to label synthetic offline training examples. Runtime
planning must load the trained GradientBoostingRegressor and must never call
this module.
"""

import numpy as np


def bootstrap_priority_label(
    severity: np.ndarray | float,
    asset_criticality: np.ndarray | float,
    overdue_factor: np.ndarray | float,
    deadline_urgency: np.ndarray | float,
) -> np.ndarray:
    """Return policy-supervised labels from normalized inputs in [0, 1]."""
    return 100.0 * (
        0.40 * np.asarray(severity)
        + 0.20 * np.asarray(overdue_factor)
        + 0.25 * np.asarray(asset_criticality)
        + 0.15 * np.asarray(deadline_urgency)
    )

