"""Train, evaluate, and persist the Gradient Boosting priority model."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

import joblib
import numpy as np
import sklearn
from sklearn.ensemble import GradientBoostingRegressor
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
from sklearn.model_selection import train_test_split

from .features import FEATURE_NAMES
from .generate_data import generate_synthetic_data


def train(config_path: Path, model_path: Path, metadata_path: Path) -> dict:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    features, labels = generate_synthetic_data(config["sample_count"], config["random_seed"])
    x_train, x_test, y_train, y_test = train_test_split(
        features,
        labels,
        test_size=config["test_size"],
        random_state=config["random_seed"],
    )
    model = GradientBoostingRegressor(**config["hyperparameters"])
    model.fit(x_train, y_train)
    predictions = np.clip(model.predict(x_test), 0.0, 100.0)
    metadata = {
        "model_name": "Gradient Boosting Regressor",
        "model_type": type(model).__name__,
        "model_version": config["model_version"],
        "training_source": "deterministic synthetic tasks with bootstrap policy labels",
        "feature_names": FEATURE_NAMES,
        "sample_count": int(config["sample_count"]),
        "training_sample_count": int(len(x_train)),
        "held_out_sample_count": int(len(x_test)),
        "random_seed": int(config["random_seed"]),
        "metrics": {
            "mae": float(mean_absolute_error(y_test, predictions)),
            "rmse": float(mean_squared_error(y_test, predictions) ** 0.5),
            "r2": float(r2_score(y_test, predictions)),
        },
        "hyperparameters": model.get_params(),
        "scikit_learn_version": sklearn.__version__,
        "trained_at": datetime.now(timezone.utc).isoformat(),
    }
    model_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(model, model_path)
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=root / "config/model.json")
    parser.add_argument("--model", type=Path, default=root / "models/priority_gbr_v1.joblib")
    parser.add_argument("--metadata", type=Path, default=root / "models/priority_gbr_v1.metadata.json")
    args = parser.parse_args()
    print(json.dumps(train(args.config, args.model, args.metadata), indent=2))


if __name__ == "__main__":
    main()

