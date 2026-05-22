# Paper 1 results directory

This directory holds every artifact for the FuzzPilot arXiv placeholder paper.

Layout:

```
runs/         per-run AFL++ output (one subdir per run_id)
microbench/   E3 mutator microbench JSON outputs
aggregated/   aggregate.py outputs (CSV / JSON)
figures/      plots.py outputs (F3 / F4 / F5 PDF)
tables/       T1 / T2 markdown tables
```

Do not delete this directory after the preprint ships — Paper 3 reuses
these runs as a regression sanity check.

To regenerate everything from raw runs:

```bash
scripts/paper01/aggregate.py all
scripts/paper01/plots.py all
```

See `docs/papers/paper01_experiments_runbook.md` for the full procedure.
