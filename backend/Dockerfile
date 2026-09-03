FROM golang:1.26-bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY backend/go.mod backend/go.sum ./backend/
RUN cd backend && go mod download
COPY backend ./backend
COPY optimizer ./optimizer
RUN mkdir -p /out && g++ -std=c++20 -O2 -Ioptimizer/include optimizer/src/main.cpp optimizer/src/engine.cpp -o /out/sih-optimizer
RUN cd backend && CGO_ENABLED=0 go build -o /out/sih-api ./cmd/api

FROM python:3.13-slim-bookworm
WORKDIR /app
COPY ml/requirements.txt /app/ml/requirements.txt
RUN pip install --no-cache-dir -r /app/ml/requirements.txt
COPY --from=build /out/sih-api /app/sih-api
COPY --from=build /out/sih-optimizer /app/sih-optimizer
COPY data/scenarios /app/data/scenarios
COPY config /app/config
COPY ml/src /app/ml/src
COPY ml/models /app/ml/models
ENV OPTIMIZER_BIN=/app/sih-optimizer DATA_ROOT=/app/data/scenarios OPTIMIZER_CONFIG=/app/config/optimizer.conf API_ADDR=:8080 ML_PYTHON=/usr/local/bin/python PROJECT_ROOT=/app ML_MODEL=/app/ml/models/priority_gbr_v1.joblib ML_MODEL_METADATA=/app/ml/models/priority_gbr_v1.metadata.json
USER 65532:65532
EXPOSE 8080
ENTRYPOINT ["/app/sih-api"]
