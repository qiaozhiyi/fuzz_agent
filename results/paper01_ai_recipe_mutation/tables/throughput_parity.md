# End-to-end throughput parity (Paper 1, §6 RQ2)

Compares median AFL++ exec/sec between baseline-afl (E1a) and full-agent (E1b). The canonical RQ2 number for the paper.

| Mode | n runs | Median exec/sec |
|---|---:|---:|
| baseline-afl | 5 | 13049.33 |
| full-agent | 5 | 13815.95 |

**Ratio (full-agent / baseline-afl): 1.059**  (gate ≥ 0.85: PASS)

