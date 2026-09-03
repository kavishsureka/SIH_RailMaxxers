package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"time"

	"github.com/sih26027/block-planner/backend/internal/httpapi"
	"github.com/sih26027/block-planner/backend/internal/optimizer"
	"github.com/sih26027/block-planner/backend/internal/store"
)

func env(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func envSeconds(key string, fallback int) time.Duration {
	value, err := strconv.Atoi(env(key, strconv.Itoa(fallback)))
	if err != nil || value < 1 {
		value = fallback
	}
	return time.Duration(value) * time.Second
}

func absoluteEnv(key, fallback string) string {
	value := env(key, fallback)
	absolute, err := filepath.Abs(value)
	if err != nil {
		return value
	}
	return absolute
}

func main() {
	ctx := context.Background()
	var database *store.Store
	var benchmarkStore httpapi.BenchmarkStore
	if databaseURL := os.Getenv("DATABASE_URL"); databaseURL != "" {
		var err error
		database, err = store.Open(ctx, databaseURL)
		if err != nil {
			log.Printf("PostgreSQL unavailable; continuing without persistence: %v", err)
		} else {
			defer database.Close()
			benchmarkStore = database
		}
	}
	runner := optimizer.CommandRunner{
		Binary:        absoluteEnv("OPTIMIZER_BIN", "../build/optimizer/sih-optimizer"),
		Config:        absoluteEnv("OPTIMIZER_CONFIG", "../config/optimizer.conf"),
		TimeLimit:     envSeconds("SOLVER_TIME_LIMIT_SECONDS", 15),
		Python:        absoluteEnv("ML_PYTHON", "../work/ml-venv/bin/python"),
		ProjectRoot:   absoluteEnv("PROJECT_ROOT", ".."),
		Model:         absoluteEnv("ML_MODEL", "../ml/models/priority_gbr_v1.joblib"),
		ModelMetadata: absoluteEnv("ML_MODEL_METADATA", "../ml/models/priority_gbr_v1.metadata.json"),
	}
	address := env("API_ADDR", ":8080")
	log.Printf("SIH planner API listening on %s", address)
	log.Fatal(http.ListenAndServe(address, httpapi.New(runner, benchmarkStore, absoluteEnv("DATA_ROOT", "../data/scenarios"))))
}
