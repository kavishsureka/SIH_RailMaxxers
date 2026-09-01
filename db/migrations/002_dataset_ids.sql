ALTER TABLE plans ADD COLUMN IF NOT EXISTS dataset_id text;
UPDATE plans SET dataset_id = 'scenario-alpha' WHERE dataset_id IS NULL;
ALTER TABLE plans ALTER COLUMN dataset_id SET NOT NULL;

ALTER TABLE benchmark_runs ADD COLUMN IF NOT EXISTS dataset_id text;
UPDATE benchmark_runs SET dataset_id = COALESCE(result->>'dataset_id', 'scenario-alpha')
WHERE dataset_id IS NULL;
ALTER TABLE benchmark_runs ALTER COLUMN dataset_id SET NOT NULL;
