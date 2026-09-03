package optimizer

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestWritePrioritiesPreservesBatchScores(t *testing.T) {
	path, err := writePriorities([]PriorityPrediction{
		{TaskID: "T001", PriorityScore: 81.25},
		{TaskID: "T002", PriorityScore: 42.5},
	})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(path)
	file, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	rows, err := csv.NewReader(file).ReadAll()
	if err != nil {
		t.Fatal(err)
	}
	if len(rows) != 3 || rows[1][0] != "T001" || rows[1][1] != "81.250000" {
		t.Fatalf("unexpected priority CSV: %#v", rows)
	}
}

func TestCommandRunnerUsesOneMLBatchForAllPlans(t *testing.T) {
	root, err := filepath.Abs("../../..")
	if err != nil {
		t.Fatal(err)
	}
	binary := filepath.Join(root, "build", "optimizer", "sih-optimizer")
	python := filepath.Join(root, "work", "ml-venv", "bin", "python")
	for _, required := range []string{binary, python, filepath.Join(root, "ml", "models", "priority_gbr_v1.joblib")} {
		if _, err := os.Stat(required); err != nil {
			t.Skipf("integration dependency not built: %s", required)
		}
	}
	runner := CommandRunner{
		Binary:        binary,
		Config:        filepath.Join(root, "config", "optimizer.conf"),
		TimeLimit:     5 * time.Second,
		Python:        python,
		ProjectRoot:   root,
		Model:         filepath.Join(root, "ml", "models", "priority_gbr_v1.joblib"),
		ModelMetadata: filepath.Join(root, "ml", "models", "priority_gbr_v1.metadata.json"),
	}
	payload, err := runner.Benchmark(context.Background(), filepath.Join(root, "data", "scenarios", "scenario-alpha"), "scenario-alpha")
	if err != nil {
		t.Fatal(err)
	}
	var result struct {
		PriorityModel  map[string]any       `json:"priority_model"`
		TaskPriorities []PriorityPrediction `json:"task_priorities"`
		Plans          []Plan               `json:"plans"`
	}
	if err := json.Unmarshal(payload, &result); err != nil {
		t.Fatal(err)
	}
	if result.PriorityModel["model_type"] != "GradientBoostingRegressor" || len(result.TaskPriorities) != 110 {
		t.Fatalf("unexpected ML envelope: model=%v predictions=%d", result.PriorityModel["model_type"], len(result.TaskPriorities))
	}
	if len(result.Plans) != 3 {
		t.Fatalf("plans = %d", len(result.Plans))
	}
	for _, plan := range result.Plans {
		if plan.PriorityModelVersion != "v1" || plan.PrioritySource != "ML Prediction" {
			t.Fatalf("plan %s missing shared ML provenance", plan.Algorithm)
		}
	}
}
