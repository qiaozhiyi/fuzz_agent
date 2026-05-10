PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS runs (
  id TEXT PRIMARY KEY,
  project TEXT,
  target_name TEXT,
  start_ts INTEGER,
  end_ts INTEGER,
  status TEXT,
  os TEXT,
  arch TEXT,
  afl_version TEXT,
  target_hash TEXT,
  seed_hash TEXT
);

CREATE TABLE IF NOT EXISTS campaigns (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  type TEXT,
  parent_campaign_id TEXT,
  intervention_id TEXT,
  output_dir TEXT,
  start_ts INTEGER,
  end_ts INTEGER,
  budget_sec INTEGER,
  status TEXT
);

CREATE TABLE IF NOT EXISTS telemetry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  campaign_id TEXT,
  ts INTEGER,
  execs_done INTEGER,
  execs_per_sec REAL,
  paths_total INTEGER,
  unique_crashes INTEGER,
  unique_hangs INTEGER,
  bitmap_cvg REAL,
  recipe_hits INTEGER,
  recipe_misses INTEGER,
  raw_json TEXT
);

CREATE INDEX IF NOT EXISTS idx_telemetry_campaign_ts
ON telemetry (campaign_id, ts);

CREATE TABLE IF NOT EXISTS plateaus (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  campaign_id TEXT,
  detected_ts INTEGER,
  window_sec INTEGER,
  execs_delta INTEGER,
  new_paths_delta INTEGER,
  reason TEXT,
  blackboard_json TEXT
);

CREATE TABLE IF NOT EXISTS interventions (
  id TEXT PRIMARY KEY,
  plateau_id TEXT,
  agent TEXT,
  action TEXT,
  params_json TEXT,
  hypothesis TEXT,
  expected_signal TEXT,
  status TEXT
);

CREATE TABLE IF NOT EXISTS seed_strategies (
  id TEXT PRIMARY KEY,
  plateau_id TEXT,
  agent TEXT,
  selector_json TEXT,
  operator_weights_json TEXT,
  offset_policy_json TEXT,
  dictionary_tokens_json TEXT,
  repair_policy_json TEXT,
  priority INTEGER,
  ttl_sec INTEGER,
  status TEXT,
  created_ts INTEGER
);

CREATE TABLE IF NOT EXISTS mutation_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  campaign_id TEXT,
  strategy_id TEXT,
  seed_id TEXT,
  recipe_id TEXT,
  operator TEXT,
  offset INTEGER,
  size_before INTEGER,
  size_after INTEGER,
  ts INTEGER
);

CREATE TABLE IF NOT EXISTS micro_results (
  id TEXT PRIMARY KEY,
  intervention_id TEXT,
  strategy_ids_json TEXT,
  campaign_id TEXT,
  execs_done INTEGER,
  new_paths INTEGER,
  new_edges INTEGER,
  unique_crashes INTEGER,
  recipe_hits INTEGER,
  recipe_misses INTEGER,
  reward REAL,
  promoted INTEGER
);

CREATE TABLE IF NOT EXISTS agent_decisions (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  plateau_id TEXT,
  agent TEXT,
  model_provider TEXT,
  model_name TEXT,
  task_json TEXT,
  context_hash TEXT,
  response_hash TEXT,
  latency_ms INTEGER,
  schema_valid INTEGER,
  fallback_used INTEGER,
  error TEXT,
  proposal_json TEXT,
  created_ts INTEGER
);

CREATE INDEX IF NOT EXISTS idx_agent_decisions_run_agent
ON agent_decisions (run_id, agent, created_ts);

CREATE TABLE IF NOT EXISTS agent_memory (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  target_name TEXT,
  agent TEXT,
  memory_type TEXT,
  key TEXT,
  value_json TEXT,
  evidence_json TEXT,
  reward REAL,
  confidence REAL,
  updated_ts INTEGER
);

CREATE INDEX IF NOT EXISTS idx_agent_memory_target_agent
ON agent_memory (target_name, agent, key);

CREATE TABLE IF NOT EXISTS agent_skills (
  id TEXT PRIMARY KEY,
  agent TEXT,
  target_family TEXT,
  skill_name TEXT,
  trigger_json TEXT,
  policy_json TEXT,
  source_decision_ids_json TEXT,
  success_count INTEGER,
  failure_count INTEGER,
  updated_ts INTEGER
);

CREATE TABLE IF NOT EXISTS crashes (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  campaign_id TEXT,
  path TEXT,
  sha256 TEXT,
  signal TEXT,
  sanitizer TEXT,
  stack_hash TEXT,
  first_seen_ts INTEGER
);
