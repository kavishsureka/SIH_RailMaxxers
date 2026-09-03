"""Batch runtime inference. This module never trains or generates labels."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import joblib
import numpy as np

from .features import FEATURE_NAMES, feature_matrix, read_tasks


def predict(tasks_path: Path, model_path: Path, metadata_path: Path) -> dict:
    tasks = read_tasks(tasks_path)
    model = joblib.load(model_path)
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if metadata["feature_names"] != FEATURE_NAMES:
        raise ValueError("model feature schema does not match runtime feature extractor")
    values = np.clip(model.predict(feature_matrix(tasks)), 0.0, 100.0)
    return {
        "priority_model": metadata,
        "predictions": [
            {
                "task_id": task["id"],
                "priority_score": round(float(value), 6),
                "priority_source": "ML Prediction",
                "priority_model_version": metadata["model_version"],
            }
            for task, value in zip(tasks, values)
        ],
    }


def write_predictions_csv(path: Path, result: dict) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["task_id", "priority_score"])
        for prediction in result["predictions"]:
            writer.writerow([prediction["task_id"], prediction["priority_score"]])


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--tasks", type=Path, required=True)
    parser.add_argument("--model", type=Path, default=root / "models/priority_gbr_v1.joblib")
    parser.add_argument("--metadata", type=Path, default=root / "models/priority_gbr_v1.metadata.json")
    parser.add_argument("--output-csv", type=Path)
    args = parser.parse_args()
    result = predict(args.tasks, args.model, args.metadata)
    if args.output_csv:
        write_predictions_csv(args.output_csv, result)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
