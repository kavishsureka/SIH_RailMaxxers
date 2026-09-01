package main

import (
	"context"
	"log"
	"net/http"
	"os"
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
		Binary:    env("OPTIMIZER_BIN", "../build/optimizer/sih-optimizer"),
		DataDir:   env("DATA_DIR", "../data/demo"),
		Config:    env("OPTIMIZER_CONFIG", "../config/optimizer.conf"),
		TimeLimit: envSeconds("SOLVER_TIME_LIMIT_SECONDS", 15),
	}
	address := env("API_ADDR", ":8080")
	log.Printf("SIH planner API listening on %s", address)
	log.Fatal(http.ListenAndServe(address, httpapi.New(runner, benchmarkStore, runner.DataDir)))
}
