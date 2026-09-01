FROM golang:1.26-bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY backend/go.mod backend/go.sum ./backend/
RUN cd backend && go mod download
COPY backend ./backend
COPY optimizer ./optimizer
RUN mkdir -p /out && g++ -std=c++20 -O2 -Ioptimizer/include optimizer/src/main.cpp optimizer/src/engine.cpp -o /out/sih-optimizer
RUN cd backend && CGO_ENABLED=0 go build -o /out/sih-api ./cmd/api

FROM gcr.io/distroless/base-debian12:nonroot
WORKDIR /app
COPY --from=build /out/sih-api /app/sih-api
COPY --from=build /out/sih-optimizer /app/sih-optimizer
COPY data/demo /app/data/demo
COPY config /app/config
ENV OPTIMIZER_BIN=/app/sih-optimizer DATA_DIR=/app/data/demo OPTIMIZER_CONFIG=/app/config/optimizer.conf API_ADDR=:8080
EXPOSE 8080
ENTRYPOINT ["/app/sih-api"]

