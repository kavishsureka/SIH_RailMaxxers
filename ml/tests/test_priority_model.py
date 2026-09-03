from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import joblib
import numpy as np

from ml.src.features import FEATURE_NAMES, extract_task_features, feature_matrix
from ml.src.generate_data import generate_synthetic_data
from ml.src.labels import bootstrap_priority_label
from ml.src.train import train


class PriorityModelTests(unittest.TestCase):
    def test_bootstrap_label_formula(self) -> None:
        actual = bootstrap_priority_label(1.0, 0.8, 0.5, 0.2)
        self.assertAlmostEqual(float(actual), 73.0)

    def test_feature_extraction(self) -> None:
        task = {"severity": "8", "criticality": "6", "due_slot": "672"}
        features = extract_task_features(task)
        self.assertEqual(len(features), len(FEATURE_NAMES))
        self.assertEqual(features[:3], [0.8, 0.6, 0.0])
        self.assertAlmostEqual(features[3], 0.75)

    def test_generation_is_deterministic_and_non_uniform(self) -> None:
        first_x, first_y = generate_synthetic_data(1000, 26027)
        second_x, second_y = generate_synthetic_data(1000, 26027)
        np.testing.assert_array_equal(first_x, second_x)
        np.testing.assert_array_equal(first_y, second_y)
        self.assertGreater(np.count_nonzero(first_x[:, 2] == 0), 700)

    def test_training_and_inference_shape_range_and_quality(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = {
                "model_version": "test-v1",
                "random_seed": 26027,
                "sample_count": 2500,
                "test_size": 0.2,
                "hyperparameters": {
                    "n_estimators": 80,
                    "learning_rate": 0.06,
                    "max_depth": 3,
                    "min_samples_leaf": 5,
                    "loss": "squared_error",
                    "random_state": 26027,
                },
            }
            config_path = root / "config.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            model_path, metadata_path = root / "model.joblib", root / "metadata.json"
            metadata = train(config_path, model_path, metadata_path)
            model = joblib.load(model_path)
            tasks = [{"severity": "10", "criticality": "9", "due_slot": "100"}]
            prediction = model.predict(feature_matrix(tasks))
            self.assertEqual(prediction.shape, (1,))
            self.assertGreaterEqual(float(prediction[0]), 0.0)
            self.assertLessEqual(float(prediction[0]), 100.0)
            self.assertLess(metadata["metrics"]["mae"], 2.0)
            self.assertGreater(metadata["metrics"]["r2"], 0.98)


if __name__ == "__main__":
    unittest.main()

