# ML priority engine

For the complete feature formulas, synthetic distributions, training/evaluation interpretation, Go/C++ handoff, limitations, and viva answers, see [`../docs/ML_PRIORITY_MODEL.md`](../docs/ML_PRIORITY_MODEL.md).

This directory owns the prototype priority pipeline. At runtime, the planner loads `models/priority_gbr_v1.joblib`; it never retrains and never calls the bootstrap label policy. Go requests one batch of scores before invoking any scheduler, so Independent, Greedy, and CP-SAT consume the same values.

The current model is a policy surrogate, not evidence of behavior learned from Indian Railways. Its deterministic synthetic examples are labelled by the bootstrap policy isolated in `src/labels.py`. A railway pilot would replace that supervision with historical planner decisions and/or outcome labels, retrain and recalibrate the model, and retain the stable `Task Features -> ML Priority -> Optimizer` interface. The feature schema may evolve as real fields become available.

## Layout

- `config/model.json` — fixed seed, sample count, split, version, and `GradientBoostingRegressor` hyperparameters.
- `src/features.py` — the only task-to-feature adapter. The v1 features are `severity`, `asset_criticality`, `overdue_factor`, and `deadline_urgency`.
- `src/labels.py` — offline-only bootstrap policy supervision.
- `src/generate_data.py` — deterministic non-uniform synthetic distributions.
- `src/train.py` — split, train, evaluate, and persist model plus metadata.
- `src/inference.py` — model-only batch prediction used by Go.
- `models/priority_gbr_v1.metadata.json` — actual held-out MAE, RMSE, R², sample counts, feature names, versions, and hyperparameters.

Current scenario CSVs do not contain maintenance-age history. The runtime extractor therefore uses `overdue_factor = 0` for their non-overdue tasks and derives deadline urgency from `due_slot`. A future source may provide an explicit normalized overdue factor without changing the Go/C++ contracts.

## Commands

Run from the repository root:

```bash
python3 -m venv work/ml-venv
work/ml-venv/bin/python -m pip install -r ml/requirements.txt
work/ml-venv/bin/python -m ml.src.generate_data --output ml/data/synthetic_priority.csv --samples 50000 --seed 26027
work/ml-venv/bin/python -m ml.src.train --config ml/config/model.json --model ml/models/priority_gbr_v1.joblib --metadata ml/models/priority_gbr_v1.metadata.json
work/ml-venv/bin/python -m ml.src.inference --tasks data/scenarios/scenario-alpha/tasks.csv
work/ml-venv/bin/python -m unittest discover -s ml/tests -v
```

The synthetic CSV is optional/reproducible and ignored by Git. Training generates the same data in memory, evaluates on the fixed held-out split, and records the measured metrics alongside the model.
