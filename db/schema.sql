PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
-- synchronous=NORMAL is the recommended WAL setting; it's still durable
-- across process crashes, only loses uncommitted data on power loss.
PRAGMA synchronous = NORMAL;
-- 5 second busy timeout absorbs the rare contention from a concurrent
-- analysis reader (and is harmless for the single-writer normal case).
PRAGMA busy_timeout = 5000;

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
  seed_hash TEXT,
  -- Outcome of the run's final winner-selection step. One of
  -- {no_candidates, all_failed, selected, no_significance}.
  winner_status TEXT,
  -- Aggregate LLM accounting for the entire run; lets the paper's
  -- RQ4 cost figures be queried with a single SELECT.
  llm_calls INTEGER DEFAULT 0,
  llm_failed_calls INTEGER DEFAULT 0,
  llm_input_tokens INTEGER DEFAULT 0,
  llm_output_tokens INTEGER DEFAULT 0,
  llm_total_latency_ms REAL DEFAULT 0,
  ablation_mode TEXT
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
  status TEXT,
  -- One of {completed, timeout_killed, signaled, spawn_failed,
  --         stats_unreadable, dry_run}. Empty for in-progress.
  termination_reason TEXT
);

CREATE INDEX IF NOT EXISTS idx_campaigns_run_type
ON campaigns (run_id, type);

CREATE INDEX IF NOT EXISTS idx_campaigns_intervention
ON campaigns (intervention_id);

CREATE TABLE IF NOT EXISTS telemetry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  campaign_id TEXT,
  ts INTEGER,
  execs_done INTEGER,
  execs_per_sec REAL,
  paths_total INTEGER,
  -- Absolute edge count (AFL++ 3.x `edges_found`). The primary signal
  -- for paper-grade coverage comparison; bitmap_cvg is kept for
  -- backward compat but should not be used for absolute claims.
  edges_found INTEGER DEFAULT 0,
  unique_crashes INTEGER,
  unique_hangs INTEGER,
  bitmap_cvg REAL,
  recipe_hits INTEGER,
  recipe_misses INTEGER,
  -- True (1) when the collector detected the sample as stale (AFL
  -- last_update too old). Such samples should be excluded from plateau
  -- and coverage analysis.
  stale INTEGER DEFAULT 0,
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
  new_edges_delta INTEGER DEFAULT 0,
  new_crashes_delta INTEGER DEFAULT 0,
  sample_count INTEGER DEFAULT 0,
  reason TEXT,
  blackboard_json TEXT
);

CREATE INDEX IF NOT EXISTS idx_plateaus_run
ON plateaus (run_id, detected_ts);

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

CREATE INDEX IF NOT EXISTS idx_interventions_plateau
ON interventions (plateau_id);

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
  -- Decomposed reward components — kept separate so offline analysis
  -- can re-weight without re-running anything.
  edge_score REAL DEFAULT 0,
  path_score REAL DEFAULT 0,
  crash_score REAL DEFAULT 0,
  recipe_score REAL DEFAULT 0,
  reward REAL,
  -- True (1) when parent run had no edges_found telemetry; the
  -- analysis should flag such runs because reward decomposition is
  -- only partial.
  edges_unavailable INTEGER DEFAULT 0,
  promoted INTEGER
);

CREATE INDEX IF NOT EXISTS idx_micro_results_intervention
ON micro_results (intervention_id);

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
  -- Token accounting (best-effort: parsed from response usage when
  -- present, otherwise a ~4 chars/token approximation).
  input_tokens INTEGER DEFAULT 0,
  output_tokens INTEGER DEFAULT 0,
  retry_count INTEGER DEFAULT 0,
  -- Coarse error classification. One of {ok, timeout, http_error,
  -- transport_error, parse_error, schema_invalid, schema_invalid_truncated,
  -- auth_error, guardrail_violation, signaled, spawn_error}.
  error_kind TEXT DEFAULT 'ok',
  schema_valid INTEGER,
  fallback_used INTEGER,
  error TEXT,
  proposal_json TEXT,
  created_ts INTEGER
);

CREATE INDEX IF NOT EXISTS idx_agent_decisions_run_agent
ON agent_decisions (run_id, agent, created_ts);

CREATE INDEX IF NOT EXISTS idx_agent_decisions_plateau
ON agent_decisions (plateau_id);

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

CREATE INDEX IF NOT EXISTS idx_crashes_run
ON crashes (run_id, first_seen_ts);
