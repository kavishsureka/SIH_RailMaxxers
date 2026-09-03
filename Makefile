ROOT_DIR := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
-include .env
export

ORTOOLS_ROOT ?= .deps/or-tools
ORTOOLS_ROOT_ABS := $(abspath $(ORTOOLS_ROOT))
SOLVER_TIME_LIMIT_SECONDS ?= 15
CMAKE_BUILD_PARALLEL_LEVEL ?= 4
OPTIMIZER_BIN_ABS := $(ROOT_DIR)/build/optimizer/sih-optimizer

.PHONY: setup env install-deps setup-ortools generate generate-presets build-optimizer build-portable \
	build-api verify-native test benchmark api web dev db-up db-down up down

setup: env install-deps verify-native

env:
	@test -f .env || { cp .env.example .env; echo "Created .env from .env.example"; }

install-deps: env
	cd frontend && npm ci
	cd backend && go mod download
	python3 -m venv work/ml-venv
	work/ml-venv/bin/python -m pip install -r ml/requirements.txt

.PHONY: train-ml test-ml
train-ml:
	work/ml-venv/bin/python -m ml.src.train

test-ml:
	work/ml-venv/bin/python -m unittest discover -s ml/tests -v

setup-ortools: env
	bash scripts/setup-ortools.sh

generate:
	python3 tools/generate_demo.py --output data/scenarios/scenario-alpha --profile alpha --seed 26027 --corridors 10 --tasks 110 --trains-per-day 105
	python3 tools/generate_demo.py --output data/scenarios/scenario-beta --profile beta --seed 26127 --corridors 10 --tasks 124 --trains-per-day 130
	python3 tools/generate_demo.py --output data/scenarios/scenario-gamma --profile gamma --seed 26227 --corridors 10 --tasks 120 --trains-per-day 115

generate-presets:
	python3 tools/generate_demo.py --output data/benchmarks/100 --seed 26100 --corridors 10 --tasks 100 --trains-per-day 100
	python3 tools/generate_demo.py --output data/benchmarks/250 --seed 26250 --corridors 10 --tasks 250 --trains-per-day 125
	python3 tools/generate_demo.py --output data/benchmarks/500 --seed 26500 --corridors 10 --tasks 500 --trains-per-day 150

build-optimizer: setup-ortools
	@test -f "$(ORTOOLS_ROOT_ABS)/lib/cmake/ortools/ortoolsConfig.cmake" || { echo "OR-Tools is missing. Run: make setup-ortools"; exit 1; }
	cmake -S . -B build -DSIH_WITH_ORTOOLS=ON -Dortools_ROOT="$(ORTOOLS_ROOT_ABS)" -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel "$(CMAKE_BUILD_PARALLEL_LEVEL)"

build-portable:
	cmake -S . -B build-portable -DSIH_WITH_ORTOOLS=OFF -DCMAKE_BUILD_TYPE=Release
	cmake --build build-portable --parallel "$(CMAKE_BUILD_PARALLEL_LEVEL)"

build-api:
	cd backend && go build -o ../build/sih-api ./cmd/api

test: build-optimizer test-ml
	ctest --test-dir build --output-on-failure
	cd backend && go test ./...
	cd frontend && npm run lint

verify-native: build-optimizer
	python3 tools/verify_native_cp_sat.py --binary "$(OPTIMIZER_BIN_ABS)" --data "$(ROOT_DIR)/data/scenarios/scenario-alpha" --config "$(ROOT_DIR)/config/optimizer.conf" --time-limit "$(SOLVER_TIME_LIMIT_SECONDS)" --python "$(ROOT_DIR)/work/ml-venv/bin/python" --project-root "$(ROOT_DIR)" --model "$(ROOT_DIR)/ml/models/priority_gbr_v1.joblib" --metadata "$(ROOT_DIR)/ml/models/priority_gbr_v1.metadata.json"

benchmark: build-optimizer
	./scripts/benchmark.sh

api: build-optimizer
	cd backend && OPTIMIZER_BIN="$(OPTIMIZER_BIN_ABS)" DATA_ROOT="$(ROOT_DIR)/data/scenarios" OPTIMIZER_CONFIG="$(ROOT_DIR)/config/optimizer.conf" go run ./cmd/api

web:
	cd frontend && npm run dev

dev: build-optimizer
	bash scripts/dev.sh

db-up:
	docker compose up -d postgres

db-down:
	docker compose stop postgres

up:
	docker compose up --build

down:
	docker compose down
