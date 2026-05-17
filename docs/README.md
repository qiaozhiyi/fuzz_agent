# FuzzPilot Docs

Keep this directory small. The top-level README is the full command reference;
these files are for orientation and evaluation planning.

## Files

- [project_brief.md](project_brief.md): scope, current status, architecture, and
  code map.
- [quickstart.md](quickstart.md): build and smoke-test path for a clean checkout.
- [evaluation.md](evaluation.md): baselines, metrics, run records, and remaining
  evaluation work.

## Current Status

FuzzPilot is an experimental AFL++ controller prototype. The implementation can
build, run local smoke tests, parse AFL++ telemetry, drive model-agent planning
outside the mutator hot path, run short micro-campaign paths, and write
SQLite/JSONL artifacts.

The missing work is benchmark-scale evidence: repeated Linux/x86_64 runs,
ablations, crash replay/deduplication, and result analysis.
