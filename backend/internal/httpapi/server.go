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

type BenchmarkStore interface {
	SaveBenchmark(context.Context, string, []byte) error
}

const defaultDatasetID = "scenario-alpha"

type datasetDefinition struct {
	ID          string `json:"id"`
	Label       string `json:"label"`
	Description string `json:"description"`
	Default     bool   `json:"default"`
	Directory   string `json:"-"`
}

var datasetDefinitions = []datasetDefinition{
	{ID: "scenario-alpha", Label: "Scenario Alpha", Description: "Moderate traffic, Engineering-heavy presentation scenario", Default: true},
	{ID: "scenario-beta", Label: "Scenario Beta", Description: "Higher traffic with stronger S&T and TRD interaction"},
	{ID: "scenario-gamma", Label: "Scenario Gamma", Description: "Dependency and power-block consolidation challenge"},
}

type Server struct {
	runner   optimizer.Runner
	store    BenchmarkStore
	dataRoot string
}

func New(runner optimizer.Runner, store BenchmarkStore, dataRoot string) http.Handler {
	s := &Server{runner: runner, store: store, dataRoot: dataRoot}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/health", s.health)
	mux.HandleFunc("GET /api/datasets", s.datasets)
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
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func (s *Server) health(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{"status": "ok", "slot_minutes": 15, "horizon_days": 28, "horizon_weeks": 4})
}

func readCSV(path string) ([]map[string]string, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	reader := csv.NewReader(file)
	rows, err := reader.ReadAll()
	if err != nil || len(rows) == 0 {
		return nil, err
	}
	result := make([]map[string]string, 0, len(rows)-1)
	for _, row := range rows[1:] {
		item := make(map[string]string, len(rows[0]))
		for i, key := range rows[0] {
			if i < len(row) {
				item[key] = row[i]
			}
		}
		result = append(result, item)
	}
	return result, nil
}

func (s *Server) resolveDataset(id string) (datasetDefinition, bool) {
	if id == "" {
		id = defaultDatasetID
	}
	for _, definition := range datasetDefinitions {
		if definition.ID == id {
			definition.Directory = filepath.Join(s.dataRoot, definition.ID)
			return definition, true
		}
	}
	return datasetDefinition{}, false
}

func requestedDatasetID(r *http.Request) (string, error) {
	if id := r.URL.Query().Get("dataset_id"); id != "" {
		return id, nil
	}
	if r.Method != http.MethodPost || r.Body == nil {
		return defaultDatasetID, nil
	}
	var request struct {
		DatasetID string `json:"dataset_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		return "", err
	}
	if request.DatasetID == "" {
		return defaultDatasetID, nil
	}
	return request.DatasetID, nil
}

func (s *Server) metadata(definition datasetDefinition) (map[string]any, error) {
	counts := make(map[string]int)
	for _, name := range []string{"corridors", "tasks", "trains"} {
		rows, err := readCSV(filepath.Join(definition.Directory, name+".csv"))
		if err != nil {
			return nil, err
		}
		counts[name] = len(rows)
	}
	return map[string]any{"id": definition.ID, "label": definition.Label, "description": definition.Description, "default": definition.Default, "task_count": counts["tasks"], "corridor_count": counts["corridors"], "train_movement_count": counts["trains"]}, nil
}

func (s *Server) datasets(w http.ResponseWriter, _ *http.Request) {
	items := make([]map[string]any, 0, len(datasetDefinitions))
	for _, item := range datasetDefinitions {
		definition, _ := s.resolveDataset(item.ID)
		metadata, err := s.metadata(definition)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		items = append(items, metadata)
	}
	writeJSON(w, http.StatusOK, map[string]any{"default_dataset_id": defaultDatasetID, "datasets": items})
}

func (s *Server) dataset(w http.ResponseWriter, r *http.Request) {
	definition, ok := s.resolveDataset(r.URL.Query().Get("dataset_id"))
	if !ok {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "unknown dataset_id"})
		return
	}
	result := map[string]any{"dataset_id": definition.ID, "dataset_label": definition.Label, "all_trains_electric": true, "slot_minutes": 15, "horizon_days": 28, "horizon_weeks": 4, "horizon_slots": 2688}
	for _, name := range []string{"corridors", "tasks", "trains", "availability", "dependencies", "compatibility"} {
		rows, err := readCSV(filepath.Join(definition.Directory, name+".csv"))
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
			return
		}
		result[name] = rows
	}
	writeJSON(w, http.StatusOK, result)
}

func (s *Server) benchmark(w http.ResponseWriter, r *http.Request) {
	datasetID, err := requestedDatasetID(r)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid request body"})
		return
	}
	definition, ok := s.resolveDataset(datasetID)
	if !ok {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "unknown dataset_id"})
		return
	}
	payload, err := s.runner.Benchmark(r.Context(), definition.Directory, definition.ID)
	if err != nil {
		writeJSON(w, http.StatusBadGateway, map[string]string{"error": err.Error()})
		return
	}
	if s.store != nil {
		if err := s.store.SaveBenchmark(r.Context(), definition.ID, payload); err != nil {
			slog.Warn("benchmark persistence failed", "error", err)
		}
	}
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(payload)
}

func (s *Server) plan(w http.ResponseWriter, r *http.Request) {
	algorithm := strings.ToLower(r.PathValue("algorithm"))
	definition, ok := s.resolveDataset(r.URL.Query().Get("dataset_id"))
	if !ok {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "unknown dataset_id"})
		return
	}
	payload, err := s.runner.Plan(r.Context(), algorithm, definition.Directory, definition.ID)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(payload)
}
