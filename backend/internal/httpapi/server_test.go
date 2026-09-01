package httpapi

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

type fakeRunner struct{}

func (fakeRunner) Benchmark(_ context.Context, _, datasetID string) ([]byte, error) {
	return []byte(`{"dataset_id":"` + datasetID + `","plans":[]}`), nil
}
func (fakeRunner) Plan(_ context.Context, _, _, datasetID string) ([]byte, error) {
	return []byte(`{"algorithm":"greedy"}`), nil
}

const testDataRoot = "../../../data/scenarios"

func TestHealth(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, testDataRoot).ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/health", nil))
	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d", recorder.Code)
	}
	var payload map[string]any
	if err := json.Unmarshal(recorder.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if payload["horizon_days"] != float64(28) {
		t.Fatalf("horizon_days = %v", payload["horizon_days"])
	}
}

func TestBenchmarkWithoutDatabase(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, testDataRoot).ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/benchmark", nil))
	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d", recorder.Code)
	}
}

func TestDatasetsListsThreeStoredScenarios(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, testDataRoot).ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/datasets", nil))
	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d: %s", recorder.Code, recorder.Body.String())
	}
	var payload struct {
		Default  string `json:"default_dataset_id"`
		Datasets []struct {
			ID    string `json:"id"`
			Tasks int    `json:"task_count"`
		} `json:"datasets"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if payload.Default != "scenario-alpha" || len(payload.Datasets) != 3 {
		t.Fatalf("unexpected catalog: %+v", payload)
	}
	for _, dataset := range payload.Datasets {
		if dataset.Tasks < 100 || dataset.Tasks > 130 {
			t.Fatalf("%s is not medium-sized: %d tasks", dataset.ID, dataset.Tasks)
		}
	}
}

func TestBenchmarkUsesExplicitDataset(t *testing.T) {
	recorder := httptest.NewRecorder()
	request := httptest.NewRequest(http.MethodPost, "/api/benchmark", strings.NewReader(`{"dataset_id":"scenario-beta"}`))
	New(fakeRunner{}, nil, testDataRoot).ServeHTTP(recorder, request)
	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d: %s", recorder.Code, recorder.Body.String())
	}
	var payload map[string]any
	if err := json.Unmarshal(recorder.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if payload["dataset_id"] != "scenario-beta" {
		t.Fatalf("dataset_id = %v", payload["dataset_id"])
	}
}

func TestUnknownDatasetIsRejected(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, testDataRoot).ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/dataset?dataset_id=missing", nil))
	if recorder.Code != http.StatusBadRequest {
		t.Fatalf("status = %d", recorder.Code)
	}
}
