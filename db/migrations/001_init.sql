CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TYPE department AS ENUM ('ENGINEERING', 'ST', 'TRD');
CREATE TYPE train_category AS ENUM ('PREMIUM_PASSENGER', 'PASSENGER', 'GOODS');
CREATE TYPE conflict_mode AS ENUM ('HARD', 'SOFT');
CREATE TYPE algorithm AS ENUM ('INDEPENDENT', 'GREEDY', 'CP_SAT');

CREATE TABLE corridors (
  id text PRIMARY KEY,
  name text NOT NULL
);

CREATE TABLE assets (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  corridor_id text NOT NULL REFERENCES corridors(id),
  department department NOT NULL,
  asset_type text NOT NULL,
  asset_code text NOT NULL UNIQUE,
  criticality smallint NOT NULL CHECK (criticality BETWEEN 1 AND 10)
);

CREATE TABLE maintenance_tasks (
  id text PRIMARY KEY,
  asset_id uuid REFERENCES assets(id),
  corridor_id text NOT NULL REFERENCES corridors(id),
  department department NOT NULL,
  source_type text NOT NULL CHECK (source_type IN ('DEFECT','PREVENTIVE','INSPECTION','EMERGENCY')),
  task_type text NOT NULL,
  duration_minutes integer NOT NULL CHECK (duration_minutes > 0 AND duration_minutes % 15 = 0),
  severity smallint NOT NULL CHECK (severity BETWEEN 1 AND 10),
  criticality smallint NOT NULL CHECK (criticality BETWEEN 1 AND 10),
  reported_at timestamptz NOT NULL DEFAULT now(),
  due_at timestamptz,
  mandatory boolean NOT NULL DEFAULT false,
  requires_power_block boolean NOT NULL DEFAULT false,
  status text NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING','SCHEDULED','COMPLETED'))
);

CREATE TABLE train_movements (
  id text PRIMARY KEY,
  train_number text NOT NULL,
  corridor_id text NOT NULL REFERENCES corridors(id),
  category train_category NOT NULL,
  start_time timestamptz NOT NULL,
  end_time timestamptz NOT NULL CHECK (end_time > start_time),
  conflict_mode conflict_mode NOT NULL,
  impact_weight integer NOT NULL CHECK (impact_weight >= 0)
  -- All trains are electric by domain assumption: no traction/diesel column exists.
);

CREATE TABLE corridor_availability (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  corridor_id text NOT NULL REFERENCES corridors(id),
  start_time timestamptz NOT NULL,
  end_time timestamptz NOT NULL CHECK (end_time > start_time)
);

CREATE TABLE task_dependencies (
  predecessor_task_id text NOT NULL REFERENCES maintenance_tasks(id),
  successor_task_id text NOT NULL REFERENCES maintenance_tasks(id),
  min_lag_minutes integer NOT NULL DEFAULT 0 CHECK (min_lag_minutes >= 0),
  PRIMARY KEY (predecessor_task_id, successor_task_id)
);

CREATE TABLE plans (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  dataset_id text NOT NULL,
  horizon_start timestamptz NOT NULL,
  horizon_end timestamptz NOT NULL,
  algorithm algorithm NOT NULL,
  solver_status text NOT NULL,
  preprocessing_ms double precision NOT NULL CHECK (preprocessing_ms >= 0),
  algorithm_ms double precision NOT NULL CHECK (algorithm_ms >= 0),
  total_runtime_ms double precision NOT NULL CHECK (total_runtime_ms >= 0),
  objective_value bigint NOT NULL,
  metrics jsonb NOT NULL,
  validation jsonb NOT NULL,
  weights jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE blocks (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  plan_id uuid NOT NULL REFERENCES plans(id) ON DELETE CASCADE,
  corridor_id text NOT NULL REFERENCES corridors(id),
  start_time timestamptz NOT NULL,
  end_time timestamptz NOT NULL CHECK (end_time > start_time)
);

CREATE TABLE block_tasks (
  block_id uuid NOT NULL REFERENCES blocks(id) ON DELETE CASCADE,
  task_id text NOT NULL REFERENCES maintenance_tasks(id),
  PRIMARY KEY (block_id, task_id)
);

CREATE TABLE benchmark_runs (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  dataset_id text NOT NULL,
  result jsonb NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);
