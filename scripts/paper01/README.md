# Paper 1 scripts

Docker is the recommended entrypoint for portable runs:

```bash
scripts/fuzzpilot_docker.sh preflight
scripts/fuzzpilot_docker.sh run-batch --exp E1a --repeats 5
scripts/fuzzpilot_docker.sh aggregate all
scripts/fuzzpilot_docker.sh plots all
```

The scripts can still be invoked directly from the repo root for native
development after a local CMake build.

## Architecture pilot entrypoints

The architecture paper uses the Stage S/A/B/C flow documented in
`docs/papers/architecture_pilot_runbook.md`.

```bash
scripts/paper01/with_model_key.sh \
  scripts/paper01/model_auth_smoke.py --config experiments/targets/libxml2/config_glm.yaml
scripts/paper01/with_model_key.sh \
  scripts/paper01/model_auth_smoke.py --config experiments/targets/libxml2/config_glm.yaml \
    --free-glm-candidates --show-response
scripts/paper01/with_model_key.sh \
  scripts/paper01/architecture_pilot_readiness.py S
scripts/paper01/with_model_key.sh \
  scripts/paper01/run_architecture_stage.sh S
```

Use `architecture_stage_status.py` while a stage is running and
`architecture_pilot_bundle.py` after a real Stage B/C passes gates.

## Files

| File | Purpose |
|---|---|
| `preflight.sh` | environment sanity check; run before any experiment |
| `with_model_key.sh` | silently reads the model key from TTY and exports it only to the child process |
| `architecture_pilot_readiness.py` | paper-readiness audit before Stage S/A/B/C |
| `model_auth_smoke.py` | real model credential smoke test without logging secrets |
| `architecture_freeze_inputs.py` | freezes and checks libxml2 Stage B/C inputs before paper runs |
| `run_architecture_stage.sh` | low-noise Stage S/A/B/C wrapper |
| `architecture_stage_status.py` | read-only Stage S/A/B/C progress and resource estimate |
| `architecture_pilot_bundle.py` | minimal paper evidence bundle checker/packer |
| `run_batch.sh` | reads `experiments/manifests/paper01_preprint.yaml`, launches all runs for one experiment ID |
| `aggregate.py` | turns per-run artifacts into paper-facing CSV / Markdown |
| `plots.py` | turns aggregated CSV into F3 / F4 / F5 PDFs |
| `modes/*.yaml` | documents what each `--ablation` mode means |

## End-to-end smoke (DRY-RUN, no compute)

```bash
scripts/fuzzpilot_docker.sh preflight
scripts/fuzzpilot_docker.sh run-batch --exp E1a --dry-run
scripts/fuzzpilot_docker.sh aggregate all
scripts/fuzzpilot_docker.sh plots all
```

A green dry-run means the plumbing is correct; you only need real compute
to fill in the data.

## Full Paper 1 sequence (real compute)

```bash
# 0. Build Docker image
scripts/fuzzpilot_docker.sh build

# 1. Preflight
scripts/fuzzpilot_docker.sh preflight

# 2. Microbench first (cheapest, de-risks throughput claim)
scripts/fuzzpilot_docker.sh run-batch --exp E3

# 3. cJSON main + ablations (single 4-way-parallel command per exp)
scripts/fuzzpilot_docker.sh run-batch --exp E1a --parallel 4
scripts/fuzzpilot_docker.sh run-batch --exp E1b --parallel 4
scripts/fuzzpilot_docker.sh run-batch --exp E2a --parallel 3
scripts/fuzzpilot_docker.sh run-batch --exp E2b --parallel 3

# 4. Aggregate + plots
scripts/fuzzpilot_docker.sh aggregate all
scripts/fuzzpilot_docker.sh plots all

# 5. Optional libpng smoke
scripts/fuzzpilot_docker.sh run-batch --exp E5
```

After step 4 the paper has all its figures and tables. See
`docs/papers/paper01_experiments_runbook.md` for acceptance criteria.

## Resuming after a crash

Every script is idempotent enough to re-invoke with `--resume`:

```bash
scripts/fuzzpilot_docker.sh run-batch --exp E1a --resume
```

Already-completed runs (per `status` file) are skipped.

## API-key safety

`preflight.sh` greps for `sk-[A-Za-z0-9_-]{20,}` patterns in tracked files.
This repo has historical leaks (see `git log` for "Sentinel" commits) so
**re-run preflight before every arxiv tag** to catch new ones.
