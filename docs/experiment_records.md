# Experiment Record Specification

## Why this is required

Fuzzing runs that involve agent decisions and model APIs are easy to make non-reproducible. If we only keep high-level notes, we cannot reliably explain why a run improved (or regressed), replay decisions, or compare interventions across commits.

## Required fields for every run

Each run record must include the following fields:

- `run_id`
- `git_commit`
- `dirty_diff`
- `config_path`
- `config_sha256`
- `target_name`
- `target_binary`
- `target_hash`
- `seed_corpus_hash`
- `afl_version`
- `os`
- `arch`
- `model_provider`
- `model_name`
- `model_endpoint`
- `prompt/context hash`
- `agent decision id`
- `plateau id`
- `intervention id`
- `micro-campaign result`
- `promoted intervention`

## JSONL event contract

Keep append-only structured JSONL logs with these event types:

- `telemetry_tick`
- `plateau_detected`
- `agent_decision`
- `micro_campaign_started`
- `micro_campaign_result`
- `intervention_promoted`

Each event should include stable IDs (`run_id`, decision/plateau/intervention IDs where relevant) and monotonic timestamps.

## Promotion criteria

Intervention promotion must be justified by measurable signals, not intuition. Promotion decisions should reference one or more of:

- `new_edges`
- `new_paths`
- `unique_crashes`
- `reward`

## Logging rule

Do **not** rely only on natural-language summaries. Human summaries are welcome, but they must be backed by structured logs and machine-readable metadata that can be replayed and audited.
