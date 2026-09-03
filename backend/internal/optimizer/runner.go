package optimizer

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"time"
)

type Plan struct {
	DatasetID            string          `json:"dataset_id"`
	Algorithm            string          `json:"algorithm"`
	SolverStatus         string          `json:"solver_status"`
	PreprocessingMS      float64         `json:"preprocessing_ms"`
	AlgorithmMS          float64         `json:"algorithm_ms"`
	TotalRuntimeMS       float64         `json:"total_runtime_ms"`
	NativeCPSAT          bool            `json:"native_cp_sat"`
	PrioritySource       string          `json:"priority_source"`
	PriorityModelVersion string          `json:"priority_model_version"`
	Validation           json.RawMessage `json:"validation"`
	Metrics              json.RawMessage `json:"metrics"`
	Placements           json.RawMessage `json:"placements"`
	Blocks               json.RawMessage `json:"blocks"`
}

type Benchmark struct {
	DatasetID    string `json:"dataset_id"`
	HorizonDays  int    `json:"horizon_days"`
	HorizonWeeks int    `json:"horizon_weeks"`
	HorizonSlots int    `json:"horizon_slots"`
	SlotMinutes  int    `json:"slot_minutes"`
	Plans        []Plan `json:"plans"`
}

type PriorityPrediction struct {
	TaskID               string  `json:"task_id"`
	PriorityScore        float64 `json:"priority_score"`
	PrioritySource       string  `json:"priority_source"`
	PriorityModelVersion string  `json:"priority_model_version"`
}

type inferenceResult struct {
	PriorityModel map[string]any       `json:"priority_model"`
	Predictions   []PriorityPrediction `json:"predictions"`
}

type Runner interface {
	Benchmark(context.Context, string, string) ([]byte, error)
	Plan(context.Context, string, string, string) ([]byte, error)
}

type CommandRunner struct {
	Binary        string
	Config        string
	TimeLimit     time.Duration
	Python        string
	ProjectRoot   string
	Model         string
	ModelMetadata string
}

func (r CommandRunner) infer(ctx context.Context, dataDir string) (inferenceResult, error) {
	python := r.Python
	if python == "" {
		python = "python3"
	}
	args := []string{"-m", "ml.src.inference", "--tasks", filepath.Join(dataDir, "tasks.csv")}
	if r.Model != "" {
		args = append(args, "--model", r.Model)
	}
	if r.ModelMetadata != "" {
		args = append(args, "--metadata", r.ModelMetadata)
	}
	command := exec.CommandContext(ctx, python, args...)
	command.Dir = r.ProjectRoot
	out, err := command.CombinedOutput()
	if err != nil {
		return inferenceResult{}, fmt.Errorf("ML priority inference failed: %w: %s", err, out)
	}
	var result inferenceResult
	if err := json.Unmarshal(out, &result); err != nil {
		return inferenceResult{}, fmt.Errorf("ML priority inference returned invalid JSON: %w", err)
	}
	if len(result.Predictions) == 0 || result.PriorityModel["model_version"] == nil {
		return inferenceResult{}, fmt.Errorf("ML priority inference returned incomplete output")
	}
	return result, nil
}

func writePriorities(predictions []PriorityPrediction) (string, error) {
	file, err := os.CreateTemp("", "railblock-priorities-*.csv")
	if err != nil {
		return "", err
	}
	path := file.Name()
	writer := csv.NewWriter(file)
	if err := writer.Write([]string{"task_id", "priority_score"}); err == nil {
		for _, prediction := range predictions {
			err = writer.Write([]string{prediction.TaskID, strconv.FormatFloat(prediction.PriorityScore, 'f', 6, 64)})
			if err != nil {
				break
			}
		}
	}
	writer.Flush()
	if err == nil {
		err = writer.Error()
	}
	if closeErr := file.Close(); err == nil {
		err = closeErr
	}
	if err != nil {
		_ = os.Remove(path)
		return "", err
	}
	return path, nil
}

func (r CommandRunner) run(ctx context.Context, command, dataDir, datasetID string) ([]byte, error) {
	timeout := r.TimeLimit + 5*time.Second
	if timeout <= 5*time.Second {
		timeout = 15 * time.Second
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	inference, err := r.infer(ctx, dataDir)
	if err != nil {
		return nil, err
	}
	prioritiesPath, err := writePriorities(inference.Predictions)
	if err != nil {
		return nil, fmt.Errorf("cannot prepare ML priorities: %w", err)
	}
	defer os.Remove(prioritiesPath)
	args := []string{command, "--data", dataDir, "--config", r.Config,
		"--priorities", prioritiesPath,
		"--time-limit", fmt.Sprintf("%d", max(1, int(r.TimeLimit.Seconds())))}
	out, err := exec.CommandContext(ctx, r.Binary, args...).CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("optimizer failed: %w: %s", err, out)
	}
	var document map[string]any
	if err := json.Unmarshal(out, &document); err != nil {
		return nil, fmt.Errorf("optimizer returned invalid JSON: %w", err)
	}
	document["dataset_id"] = datasetID
	document["priority_model"] = inference.PriorityModel
	document["task_priorities"] = inference.Predictions
	modelVersion, _ := inference.PriorityModel["model_version"].(string)
	if plans, ok := document["plans"].([]any); ok {
		for _, value := range plans {
			if plan, ok := value.(map[string]any); ok {
				plan["dataset_id"] = datasetID
				plan["priority_source"] = "ML Prediction"
				plan["priority_model_version"] = modelVersion
			}
		}
	} else {
		document["priority_source"] = "ML Prediction"
		document["priority_model_version"] = modelVersion
	}
	return json.Marshal(document)
}

func (r CommandRunner) Benchmark(ctx context.Context, dataDir, datasetID string) ([]byte, error) {
	return r.run(ctx, "benchmark", dataDir, datasetID)
}

func (r CommandRunner) Plan(ctx context.Context, algorithm, dataDir, datasetID string) ([]byte, error) {
	if algorithm != "independent" && algorithm != "greedy" && algorithm != "cp-sat" {
		return nil, fmt.Errorf("unknown algorithm %q", algorithm)
	}
	return r.run(ctx, algorithm, dataDir, datasetID)
}
