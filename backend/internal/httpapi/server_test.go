package httpapi

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
)

type fakeRunner struct{}
func (fakeRunner) Benchmark(context.Context) ([]byte, error) { return []byte(`{"plans":[]}`), nil }
func (fakeRunner) Plan(context.Context, string) ([]byte, error) { return []byte(`{"algorithm":"greedy"}`), nil }

func TestHealth(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, "../../../data/demo").ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/health", nil))
	if recorder.Code != http.StatusOK { t.Fatalf("status = %d", recorder.Code) }
}

func TestBenchmarkWithoutDatabase(t *testing.T) {
	recorder := httptest.NewRecorder()
	New(fakeRunner{}, nil, "../../../data/demo").ServeHTTP(recorder, httptest.NewRequest(http.MethodGet, "/api/benchmark", nil))
	if recorder.Code != http.StatusOK { t.Fatalf("status = %d", recorder.Code) }
}
