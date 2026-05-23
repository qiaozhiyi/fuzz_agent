"""Compute the statistical numbers the paper review demanded:

  - Mann-Whitney U + p-value, baseline vs each treatment (plateau & last_find)
  - Vargha-Delaney A12 effect size
  - Bootstrap 95% CI for medians (BCa, 10k resamples)
  - Per-run first-reaches-N-edges times (N in 264, 268, 269)
  - Leave-one-out median exec/s (drop r05 outlier check)
  - Correct schema_valid_rate per full-agent run from agent_decisions.jsonl
  - Per-run plateau ranges & medians for all 4 modes

Run from repo root:
  python3 scripts/paper01/review_stats.py
"""

from __future__ import annotations

import csv
import json
import math
import random
import statistics
from itertools import combinations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNS = ROOT / "results" / "paper01_ai_recipe_mutation" / "runs"

MODES = {
    "baseline-afl": [f"p1_e1_cjson_baseline-afl_r{r:02d}" for r in range(1, 6)],
    "full-agent": [f"p1_e1_cjson_full-agent_r{r:02d}" for r in range(1, 6)],
    "rule-only": [f"p1_e2_cjson_rule-only_r{r:02d}" for r in range(1, 4)],
    "no-mutator": [f"p1_e2_cjson_no-mutator_r{r:02d}" for r in range(1, 4)],
}


def read_fuzzer_stats(run_id: str) -> dict[str, str]:
    out: dict[str, str] = {}
    path = RUNS / run_id / "fuzzer_stats"
    if not path.is_file():
        return out
    for line in path.read_text().splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def read_coverage_timeseries(run_id: str) -> list[tuple[float, int]]:
    """Return list of (rel_time_s, edges_found) sorted by time."""
    path = RUNS / run_id / "coverage.csv"
    if not path.is_file():
        return []
    rows: list[tuple[float, int]] = []
    with open(path) as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            ts = row.get("ts") or row.get("timestamp") or row.get("time")
            ed = row.get("edges_found") or row.get("edges")
            if ts is None or ed is None:
                continue
            try:
                rows.append((float(ts), int(ed)))
            except ValueError:
                continue
    rows.sort()
    return rows


def plateau_metrics() -> dict[str, dict]:
    """Per-mode plateau / last_find / execs / cycles."""
    out: dict[str, dict] = {}
    for mode, runs in MODES.items():
        plateaus, lasts, eps, cycles, execs = [], [], [], [], []
        per_run = []
        for r in runs:
            s = read_fuzzer_stats(r)
            if not s:
                continue
            st = int(s["start_time"])
            rt = int(s["run_time"])
            lf = int(s["last_find"])
            lf_rel = lf - st
            plateau = rt - lf_rel
            plateaus.append(plateau)
            lasts.append(lf_rel)
            eps.append(float(s["execs_per_sec"]))
            cycles.append(int(s["cycles_done"]))
            execs.append(int(s["execs_done"]))
            per_run.append({
                "run": r,
                "run_time": rt,
                "last_find_rel": lf_rel,
                "plateau": plateau,
                "execs_per_sec": float(s["execs_per_sec"]),
                "cycles_done": int(s["cycles_done"]),
                "execs_done": int(s["execs_done"]),
            })
        out[mode] = {
            "n": len(plateaus),
            "plateaus": plateaus,
            "last_finds": lasts,
            "execs_per_sec": eps,
            "cycles": cycles,
            "execs_done": execs,
            "median_plateau": statistics.median(plateaus),
            "median_last_find": statistics.median(lasts),
            "median_eps": statistics.median(eps),
            "median_cycles": statistics.median(cycles),
            "median_execs": statistics.median(execs),
            "per_run": per_run,
        }
    return out


# --------------------------------------------------------------------------- #
# Mann-Whitney U + p-value (two-sided exact for small N) + A12 effect size
# --------------------------------------------------------------------------- #

def mannwhitney_u(x: list[float], y: list[float]) -> tuple[float, float]:
    """Return (U, p_two_sided). Uses exact enumeration for small N (<=12 per arm).

    Formula matches scipy.stats.mannwhitneyu (use_continuity=False, alternative='two-sided').
    """
    nx, ny = len(x), len(y)
    combined = [(v, 0) for v in x] + [(v, 1) for v in y]
    combined.sort()
    ranks = [0.0] * len(combined)
    i = 0
    while i < len(combined):
        j = i
        while j + 1 < len(combined) and combined[j + 1][0] == combined[i][0]:
            j += 1
        avg = (i + j) / 2 + 1
        for k in range(i, j + 1):
            ranks[k] = avg
        i = j + 1
    R1 = sum(r for r, (_, lbl) in zip(ranks, combined) if lbl == 0)
    U1 = R1 - nx * (nx + 1) / 2
    U2 = nx * ny - U1
    U = min(U1, U2)
    # Exact two-sided p via enumeration when feasible.
    if nx + ny <= 16:
        all_vals = x + y
        count_le = 0
        total = 0
        for picks in combinations(range(nx + ny), nx):
            xx = [all_vals[i] for i in picks]
            yy = [all_vals[i] for i in range(nx + ny) if i not in picks]
            # Recompute U with ranks
            comb = [(v, 0) for v in xx] + [(v, 1) for v in yy]
            comb.sort()
            r = [0.0] * (nx + ny)
            i = 0
            while i < nx + ny:
                j = i
                while j + 1 < nx + ny and comb[j + 1][0] == comb[i][0]:
                    j += 1
                a = (i + j) / 2 + 1
                for k in range(i, j + 1):
                    r[k] = a
                i = j + 1
            R = sum(rr for rr, (_, lbl) in zip(r, comb) if lbl == 0)
            U_perm = R - nx * (nx + 1) / 2
            U_perm = min(U_perm, nx * ny - U_perm)
            if U_perm <= U:
                count_le += 1
            total += 1
        p = count_le / total
    else:
        # Normal approximation
        mean = nx * ny / 2
        sd = math.sqrt(nx * ny * (nx + ny + 1) / 12)
        z = abs(U - mean) / sd
        p = 2 * (1 - 0.5 * (1 + math.erf(z / math.sqrt(2))))
    return U, p


def vargha_delaney_a12(treatment: list[float], control: list[float]) -> float:
    """A12 = P(treatment > control) + 0.5 P(treatment == control).

    Larger A12 means treatment > control (longer plateaus).
    We report A12(baseline, treatment) so >0.5 means baseline > treatment (treatment is faster).
    """
    n_t, n_c = len(treatment), len(control)
    s = 0.0
    for a in treatment:
        for b in control:
            if a > b:
                s += 1
            elif a == b:
                s += 0.5
    return s / (n_t * n_c)


def bootstrap_median_ci(xs: list[float], n_iter: int = 10_000,
                        alpha: float = 0.05, seed: int = 42) -> tuple[float, float]:
    """Percentile bootstrap 95% CI for median."""
    rng = random.Random(seed)
    n = len(xs)
    medians = []
    for _ in range(n_iter):
        sample = [xs[rng.randrange(n)] for _ in range(n)]
        medians.append(statistics.median(sample))
    medians.sort()
    lo = medians[int(alpha / 2 * n_iter)]
    hi = medians[int((1 - alpha / 2) * n_iter)]
    return lo, hi


# --------------------------------------------------------------------------- #
# Time-to-N-edges
# --------------------------------------------------------------------------- #

def time_to_n_edges(run_id: str, n_target: int) -> float | None:
    """Return seconds (rel to start) when edges_found first reaches n_target, else None."""
    ts_edges = read_coverage_timeseries(run_id)
    if not ts_edges:
        return None
    t0 = ts_edges[0][0]
    for ts, edges in ts_edges:
        if edges >= n_target:
            return ts - t0
    return None


def time_to_n_table() -> dict[str, dict[int, list[float | None]]]:
    out: dict[str, dict[int, list]] = {}
    for mode, runs in MODES.items():
        out[mode] = {}
        for n in (264, 268, 269):
            out[mode][n] = [time_to_n_edges(r, n) for r in runs]
    return out


# --------------------------------------------------------------------------- #
# Schema valid rate (correct)
# --------------------------------------------------------------------------- #

def schema_valid_for_run(run_id: str) -> tuple[int, int] | None:
    """Return (valid, total) LLM-call records, or None if no decisions file."""
    path = RUNS / run_id / "agent_decisions.jsonl"
    if not path.is_file():
        return None
    text = path.read_text()
    decoder = json.JSONDecoder()
    valid, total = 0, 0
    idx = 0
    while idx < len(text):
        while idx < len(text) and text[idx] in (" ", "\t", "\n", "\r"):
            idx += 1
        if idx >= len(text):
            break
        try:
            obj, end = decoder.raw_decode(text, idx)
            idx = end
        except json.JSONDecodeError:
            nl = text.find("\n", idx)
            if nl == -1:
                break
            idx = nl + 1
            continue
        if "schema_valid" not in obj:
            continue
        total += 1
        if obj.get("schema_valid"):
            valid += 1
    return valid, total


# --------------------------------------------------------------------------- #
# Microbench equivalence (TOST)
# --------------------------------------------------------------------------- #

MICROBENCH_CSV = ROOT / "results" / "paper01_ai_recipe_mutation" / "aggregated" / "microbench.csv"


def _t_sf(t: float, df: float) -> float:
    """Upper-tail survival function for Student-t (no scipy)."""
    if t == 0:
        return 0.5
    x = df / (df + t * t)
    a, b = df / 2.0, 0.5

    def betacf(x: float, a: float, b: float) -> float:
        FPMIN, EPS, MAXIT = 1e-30, 3e-16, 200
        qab, qap, qam = a + b, a + 1, a - 1
        c = 1.0
        d = 1.0 - qab * x / qap
        if abs(d) < FPMIN:
            d = FPMIN
        d = 1.0 / d
        h = d
        for m in range(1, MAXIT + 1):
            m2 = 2 * m
            aa = m * (b - m) * x / ((qam + m2) * (a + m2))
            d = 1 + aa * d
            if abs(d) < FPMIN:
                d = FPMIN
            c = 1 + aa / c
            if abs(c) < FPMIN:
                c = FPMIN
            d = 1 / d
            h *= d * c
            aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
            d = 1 + aa * d
            if abs(d) < FPMIN:
                d = FPMIN
            c = 1 + aa / c
            if abs(c) < FPMIN:
                c = FPMIN
            d = 1 / d
            delta = d * c
            h *= delta
            if abs(delta - 1) < EPS:
                break
        return h

    bt = math.exp(
        math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b)
        + a * math.log(x) + b * math.log(1 - x)
    )
    if x < (a + 1) / (a + b + 2):
        Ix = bt * betacf(x, a, b) / a
    else:
        Ix = 1 - bt * betacf(1 - x, b, a) / b
    half = 0.5 * Ix
    return half if t > 0 else 1 - half


def welch_t(x: list[float], y: list[float]) -> tuple[float, float, float]:
    n1, n2 = len(x), len(y)
    m1, m2 = statistics.mean(x), statistics.mean(y)
    v1, v2 = statistics.variance(x), statistics.variance(y)
    se = math.sqrt(v1 / n1 + v2 / n2)
    t = (m1 - m2) / se
    df = (v1 / n1 + v2 / n2) ** 2 / ((v1 / n1) ** 2 / (n1 - 1) + (v2 / n2) ** 2 / (n2 - 1))
    return t, df, se


def tost_equivalence(x: list[float], y: list[float], delta_abs: float) -> tuple[float, float, float]:
    """Two one-sided test for equivalence within ±delta_abs.

    Schuirmann (1987) formulation:
      H0_lower: diff = (mean_x - mean_y) <= -delta_abs  (reject if diff > -delta_abs)
        t1 = (diff + delta_abs) / SE        → p1 = P(T > t1) = sf(t1, df)
      H0_upper: diff >= +delta_abs                       (reject if diff <  +delta_abs)
        t2 = (delta_abs - diff) / SE        → p2 = P(T > t2) = sf(t2, df)

    Equivalence established at level α iff max(p1, p2) < α.
    Returns (p_lower, p_upper, p_max).
    """
    t, df, se = welch_t(x, y)
    m1, m2 = statistics.mean(x), statistics.mean(y)
    diff = m1 - m2
    t1 = (diff + delta_abs) / se
    t2 = (delta_abs - diff) / se
    p_lower = _t_sf(t1, df)
    p_upper = _t_sf(t2, df)
    return p_lower, p_upper, max(p_lower, p_upper)


def microbench_arms() -> dict[str, list[float]]:
    arms: dict[str, list[float]] = {}
    if not MICROBENCH_CSV.is_file():
        return arms
    with open(MICROBENCH_CSV) as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            cfg = row.get("config")
            eps = row.get("mean_exec_per_sec")
            if not cfg or not eps:
                continue
            try:
                arms.setdefault(cfg, []).append(float(eps))
            except ValueError:
                continue
    return arms


# --------------------------------------------------------------------------- #
# Main report
# --------------------------------------------------------------------------- #

def main() -> None:
    metrics = plateau_metrics()
    baseline = metrics["baseline-afl"]

    print("=" * 78)
    print("PLATEAU & ENDPOINT MEDIANS")
    print("=" * 78)
    for mode, m in metrics.items():
        print(f"{mode:14s} N={m['n']}")
        print(f"  plateaus     : {sorted(m['plateaus'])}  median={m['median_plateau']}")
        print(f"  last_finds   : {sorted(m['last_finds'])}  median={m['median_last_find']}")
        print(f"  execs_per_sec: median={m['median_eps']:.1f}")
        print(f"  cycles_done  : median={m['median_cycles']}")
        print(f"  execs_done   : median={m['median_execs']:,}")

    print()
    print("=" * 78)
    print("MANN-WHITNEY U + A12 EFFECT SIZE (baseline vs treatment, plateau)")
    print("=" * 78)
    print("A12 = P(baseline_plateau > treatment_plateau) — >0.5 ⇒ treatment is faster")
    for mode in ("full-agent", "rule-only", "no-mutator"):
        bs = baseline["plateaus"]
        tr = metrics[mode]["plateaus"]
        U, p = mannwhitney_u(bs, tr)
        a12 = vargha_delaney_a12(bs, tr)
        print(f"  baseline vs {mode:12s}: U={U:.1f} p={p:.3f} A12={a12:.3f}")

    print()
    print("=" * 78)
    print("MANN-WHITNEY U + A12 (baseline vs treatment, last_find — bigger is better)")
    print("=" * 78)
    for mode in ("full-agent", "rule-only", "no-mutator"):
        bs = baseline["last_finds"]
        tr = metrics[mode]["last_finds"]
        U, p = mannwhitney_u(tr, bs)  # treatment > baseline desired
        a12 = vargha_delaney_a12(tr, bs)
        print(f"  baseline vs {mode:12s}: U={U:.1f} p={p:.3f} A12={a12:.3f} (treatment last_find > baseline)")

    print()
    print("=" * 78)
    print("BOOTSTRAP 95% CI FOR MEDIANS (10,000 resamples)")
    print("=" * 78)
    for mode, m in metrics.items():
        lo_p, hi_p = bootstrap_median_ci(m["plateaus"])
        lo_l, hi_l = bootstrap_median_ci(m["last_finds"])
        print(f"  {mode:14s} plateau median 95%CI=[{lo_p}, {hi_p}]  last_find 95%CI=[{lo_l}, {hi_l}]")

    print()
    print("=" * 78)
    print("TIME-TO-N-EDGES (per run, seconds rel to start)")
    print("=" * 78)
    t2n = time_to_n_table()
    for mode in MODES:
        print(f"  {mode}")
        for n in (264, 268, 269):
            vals = t2n[mode][n]
            present = [v for v in vals if v is not None]
            med = statistics.median(present) if present else None
            print(f"    edges>={n}: per_run={[round(v) if v is not None else None for v in vals]}  median={round(med) if med else None}")

    print()
    print("=" * 78)
    print("SCHEMA VALID RATE (corrected, from agent_decisions.jsonl)")
    print("=" * 78)
    fa_total_valid, fa_total_total = 0, 0
    for r in MODES["full-agent"]:
        sv = schema_valid_for_run(r)
        print(f"  {r}: {sv}")
        if sv:
            fa_total_valid += sv[0]
            fa_total_total += sv[1]
    if fa_total_total:
        print(f"  full-agent aggregate: {fa_total_valid}/{fa_total_total} = {fa_total_valid/fa_total_total*100:.1f}%")
    print()
    for r in MODES["rule-only"]:
        sv = schema_valid_for_run(r)
        print(f"  {r}: {sv}")

    print()
    print("=" * 78)
    print("LEAVE-ONE-OUT EXECS/SEC MEDIAN (full-agent r05 outlier check)")
    print("=" * 78)
    for mode in ("baseline-afl", "full-agent"):
        eps = metrics[mode]["execs_per_sec"]
        med_all = statistics.median(eps)
        for i, r in enumerate(MODES[mode]):
            others = eps[:i] + eps[i+1:]
            med_drop = statistics.median(others)
            print(f"  {mode} drop {r}: median={med_drop:.1f} (full={med_all:.1f})")

    print()
    print("=" * 78)
    print("RATIO full-agent / baseline-afl execs/sec (with/without r05)")
    print("=" * 78)
    bs_med = statistics.median(metrics["baseline-afl"]["execs_per_sec"])
    fa_eps = metrics["full-agent"]["execs_per_sec"]
    fa_med = statistics.median(fa_eps)
    print(f"  full median ratio: {fa_med/bs_med:.4f}x")
    fa_no_r05 = [v for v, r in zip(fa_eps, MODES["full-agent"]) if not r.endswith("_r05")]
    fa_med_no_r05 = statistics.median(fa_no_r05)
    print(f"  no-r05 ratio: {fa_med_no_r05/bs_med:.4f}x")

    print()
    print("=" * 78)
    print("MICROBENCH (E3) — fp-active vs fp-empty TOST equivalence")
    print("=" * 78)
    arms = microbench_arms()
    if "fp-active" in arms and "fp-empty" in arms:
        fp_a, fp_e = arms["fp-active"], arms["fp-empty"]
        m_a, m_e = statistics.mean(fp_a), statistics.mean(fp_e)
        med_a, med_e = statistics.median(fp_a), statistics.median(fp_e)
        t, df, se = welch_t(fp_a, fp_e)
        p_two = 2 * _t_sf(abs(t), df)
        print(f"  fp-active : mean={m_a:.0f}  median={med_a:.0f}  N={len(fp_a)}")
        print(f"  fp-empty  : mean={m_e:.0f}  median={med_e:.0f}  N={len(fp_e)}")
        print(f"  mean ratio  active/empty: {m_a/m_e:.4f}x ({(m_a/m_e - 1)*100:+.2f}%)")
        print(f"  median ratio active/empty: {med_a/med_e:.4f}x ({(med_a/med_e - 1)*100:+.2f}%)")
        print(f"  Welch two-sided t-test: t={t:.4f}, df={df:.2f}, p={p_two:.4f}")
        for band in (0.05, 0.10, 0.15, 0.25, 0.50):
            delta = band * m_e
            pl, pu, pmax = tost_equivalence(fp_a, fp_e, delta)
            verdict = "EQUIV @α=0.05" if pmax < 0.05 else "not equivalent"
            print(f"  TOST ±{band*100:5.1f}% (Δ={delta:.0f}): p_lower={pl:.4f} p_upper={pu:.4f} p_max={pmax:.4f}  → {verdict}")
    else:
        print("  microbench.csv not loaded — skipping TOST")


if __name__ == "__main__":
    main()
