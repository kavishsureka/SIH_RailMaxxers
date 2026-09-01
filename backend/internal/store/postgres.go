package store

import (
	"context"
	"encoding/json"

	"github.com/jackc/pgx/v5/pgxpool"
)

type Store struct{ pool *pgxpool.Pool }

func Open(ctx context.Context, databaseURL string) (*Store, error) {
	pool, err := pgxpool.New(ctx, databaseURL)
	if err != nil {
		return nil, err
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, err
	}
	return &Store{pool: pool}, nil
}

func (s *Store) Close() { s.pool.Close() }

func (s *Store) SaveBenchmark(ctx context.Context, datasetID string, payload []byte) error {
	var document any
	if err := json.Unmarshal(payload, &document); err != nil {
		return err
	}
	_, err := s.pool.Exec(ctx, `INSERT INTO benchmark_runs (dataset_id, result) VALUES ($1, $2)`, datasetID, document)
	return err
}
