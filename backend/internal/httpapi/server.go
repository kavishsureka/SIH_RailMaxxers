package httpapi

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"log/slog"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/sih26027/block-planner/backend/internal/optimizer"
)

type BenchmarkStore interface{ SaveBenchmark(context.Context, []byte) error }

type Server struct {
	runner  optimizer.Runner
	store   BenchmarkStore
	dataDir string
}

func New(runner optimizer.Runner, store BenchmarkStore, dataDir string) http.Handler {
	s := &Server{runner: runner, store: store, dataDir: dataDir}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/health", s.health)
	mux.HandleFunc("GET /api/dataset", s.dataset)
	mux.HandleFunc("GET /api/benchmark", s.benchmark)
	mux.HandleFunc("POST /api/benchmark", s.benchmark)
	mux.HandleFunc("GET /api/plans/{algorithm}", s.plan)
	return cors(mux)
}

func cors(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		if r.Method == http.MethodOptions { w.WriteHeader(http.StatusNoContent); return }
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func (s *Server) health(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{"status": "ok", "slot_minutes": 15, "horizon_days": 7})
}

func readCSV(path string) ([]map[string]string, error) {
	file, err := os.Open(path)
	if err != nil { return nil, err }
	defer file.Close()
	reader := csv.NewReader(file)
	rows, err := reader.ReadAll()
	if err != nil || len(rows) == 0 { return nil, err }
	result := make([]map[string]string, 0, len(rows)-1)
	for _, row := range rows[1:] {
		item := make(map[string]string, len(rows[0]))
		for i, key := range rows[0] { if i < len(row) { item[key] = row[i] } }
		result = append(result, item)
	}
	return result, nil
}

func (s *Server) dataset(w http.ResponseWriter, _ *http.Request) {
	result := map[string]any{"all_trains_electric": true, "slot_minutes": 15, "horizon_slots": 672}
	for _, name := range []string{"corridors", "tasks", "trains", "availability", "dependencies", "compatibility"} {
		rows, err := readCSV(filepath.Join(s.dataDir, name+".csv"))
		if err != nil { writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()}); return }
		result[name] = rows
	}
	writeJSON(w, http.StatusOK, result)
}

func (s *Server) benchmark(w http.ResponseWriter, r *http.Request) {
	payload, err := s.runner.Benchmark(r.Context())
	if err != nil { writeJSON(w, http.StatusBadGateway, map[string]string{"error": err.Error()}); return }
	if s.store != nil {
		if err := s.store.SaveBenchmark(r.Context(), payload); err != nil { slog.Warn("benchmark persistence failed", "error", err) }
	}
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(payload)
}

func (s *Server) plan(w http.ResponseWriter, r *http.Request) {
	algorithm := strings.ToLower(r.PathValue("algorithm"))
	payload, err := s.runner.Plan(r.Context(), algorithm)
	if err != nil { writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()}); return }
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(payload)
}

