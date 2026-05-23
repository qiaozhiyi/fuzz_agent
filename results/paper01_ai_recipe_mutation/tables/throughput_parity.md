# End-to-end throughput parity (Paper 1, §6 RQ2)

Compares median AFL++ `execs_per_sec` between baseline-afl (E1a) and full-agent (E1b). The canonical RQ2 number for the paper. Numbers below are from each run's final `fuzzer_stats:execs_per_sec` field; aggregated by hand pending a fix to `scripts/prepare_paper01_data.py` (the auto-generated `throughput_parity.md` rolled up `execs_done`-as-rate instead of `execs_per_sec`).

| Mode | n runs | Median exec/sec | Mean exec/sec | SD | CV |
|---|---:|---:|---:|---:|---:|
| baseline-afl (E1a) | 5 | 13,049 | 13,159 | 403 | 3.06% |
| full-agent (E1b) | 5 | 13,816 | 14,089 | 862 | 6.12% |

**Ratio E1b / E1a (median): 1.0587×** — well above the manifest acceptance gate of ≥ 0.85.

Per-run values (from `fuzzer_stats:execs_per_sec`):

| Mode | r01 | r02 | r03 | r04 | r05 |
|---|---:|---:|---:|---:|---:|
| baseline-afl | 13,049 | 12,958 | 13,108 | 12,826 | 13,854 |
| full-agent | 13,816 | 13,542 | 13,540 | 13,949 | 15,597 |
