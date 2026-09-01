package httpapi

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

type fakeRunner struct{}

func (fakeRunner) Benchmark(context.Context) ([]byte, error) { return []byte(`{"plans":[]}`), nil }
func (fakeRunner) Plan(context.Context, string) ([]byte, error) {
	return []byte(`{"algorithm":"greedy"}`), nil
}

func TestHealth(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, "../../../data/demo").ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/health", nil))
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
	New(fakeRunner{}, nil, "../../../data/demo").ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/benchmark", nil))
	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d", recorder.Code)
	}
}
