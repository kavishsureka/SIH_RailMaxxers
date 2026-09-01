package optimizer

import (
	"context"
	"encoding/json"
	"fmt"
	"os/exec"
	"time"
)

type Plan struct {
	Algorithm       string          `json:"algorithm"`
	SolverStatus    string          `json:"solver_status"`
	PreprocessingMS float64         `json:"preprocessing_ms"`
	AlgorithmMS     float64         `json:"algorithm_ms"`
	TotalRuntimeMS  float64         `json:"total_runtime_ms"`
	NativeCPSAT     bool            `json:"native_cp_sat"`
	Validation      json.RawMessage `json:"validation"`
	Metrics         json.RawMessage `json:"metrics"`
	Placements      json.RawMessage `json:"placements"`
	Blocks          json.RawMessage `json:"blocks"`
}

type Benchmark struct {
	HorizonDays  int    `json:"horizon_days"`
	HorizonWeeks int    `json:"horizon_weeks"`
	HorizonSlots int    `json:"horizon_slots"`
	SlotMinutes  int    `json:"slot_minutes"`
	Plans        []Plan `json:"plans"`
}

type Runner interface {
	Benchmark(context.Context) ([]byte, error)
	Plan(context.Context, string) ([]byte, error)
}

type CommandRunner struct {
	Binary    string
	DataDir   string
	Config    string
	TimeLimit time.Duration
}

func (r CommandRunner) run(ctx context.Context, command string) ([]byte, error) {
	timeout := r.TimeLimit + 5*time.Second
	if timeout <= 5*time.Second {
		timeout = 15 * time.Second
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	args := []string{command, "--data", r.DataDir, "--config", r.Config,
		"--time-limit", fmt.Sprintf("%d", max(1, int(r.TimeLimit.Seconds())))}
	out, err := exec.CommandContext(ctx, r.Binary, args...).CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("optimizer failed: %w: %s", err, out)
	}
	if !json.Valid(out) {
		return nil, fmt.Errorf("optimizer returned invalid JSON")
	}
	return out, nil
}

func (r CommandRunner) Benchmark(ctx context.Context) ([]byte, error) { return r.run(ctx, "benchmark") }

func (r CommandRunner) Plan(ctx context.Context, algorithm string) ([]byte, error) {
	if algorithm != "independent" && algorithm != "greedy" && algorithm != "cp-sat" {
		return nil, fmt.Errorf("unknown algorithm %q", algorithm)
	}
	return r.run(ctx, algorithm)
}
