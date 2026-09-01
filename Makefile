.PHONY: generate build-optimizer build-api test benchmark api web up down

generate:
	python3 tools/generate_demo.py --output data/demo

build-optimizer:
	cmake -S . -B build -DSIH_WITH_ORTOOLS=OFF
	cmake --build build -j

build-api:
	cd backend && go build -o ../build/sih-api ./cmd/api

test: build-optimizer
	ctest --test-dir build --output-on-failure
	cd backend && go test ./...

benchmark: build-optimizer
	./scripts/benchmark.sh

api: build-optimizer
	cd backend && OPTIMIZER_BIN=../build/optimizer/sih-optimizer go run ./cmd/api

web:
	cd frontend && npm run dev

up:
	docker compose up --build

down:
	docker compose down

