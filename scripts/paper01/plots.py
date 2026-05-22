#!/usr/bin/env python3
"""Paper 1 — plot F3 / F4 / F5 from aggregated CSV.

Reads:
  aggregated/coverage_timeseries.csv  → figures/F3_cjson_coverage.pdf
  aggregated/plateau_recovery.csv     → figures/F4_plateau_recovery.pdf
  aggregated/microbench.csv           → figures/F5_microbench.pdf

Designed to fail gracefully on missing or partial data: prints a warning
and skips the figure instead of crashing. This lets you run plots.py while
the batch is still in progress to monitor data shape.

Requires: matplotlib, pandas.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "results" / "paper01_ai_recipe_mutation"
AGG = RESULTS / "aggregated"
FIG = RESULTS / "figures"


def _import_or_die():
    try:
        import pandas as pd  # noqa: F401
        import matplotlib.pyplot as plt  # noqa: F401
    except ImportError as e:
        print(f"missing dep: {e}; run: pip install pandas matplotlib --break-system-packages", file=sys.stderr)
        sys.exit(2)


def f3_coverage(args):
    import pandas as pd
    import matplotlib.pyplot as plt

    src = AGG / "coverage_timeseries.csv"
    if not src.exists() or src.stat().st_size == 0:
        print(f"F3: {src} missing or empty — skipping")
        return
    df = pd.read_csv(src)
    if df.empty:
        print("F3: empty dataframe — skipping")
        return

    FIG.mkdir(parents=True, exist_ok=True)
    # convert ts -> seconds from run start per run_id
    df["ts"] = pd.to_numeric(df["ts"], errors="coerce")
    df["bitmap_cvg"] = pd.to_numeric(df["bitmap_cvg"].astype(str).str.rstrip("%"), errors="coerce")
    df = df.dropna(subset=["ts", "bitmap_cvg"])
    df["t0"] = df.groupby("run_id")["ts"].transform("min")
    df["elapsed_sec"] = df["ts"] - df["t0"]

    fig, ax = plt.subplots(figsize=(6, 4))
    for mode, sub in df.groupby("mode"):
        sub = sub.sort_values("elapsed_sec")
        # binned median+IQR over time
        bins = sub["elapsed_sec"] // 60  # 1-min buckets
        agg = sub.groupby(bins)["bitmap_cvg"].agg(["median", lambda s: s.quantile(0.25), lambda s: s.quantile(0.75)])
        agg.columns = ["median", "q25", "q75"]
        ax.plot(agg.index, agg["median"], label=mode)
        ax.fill_between(agg.index, agg["q25"], agg["q75"], alpha=0.2)
    ax.set_xlabel("elapsed (minutes)")
    ax.set_ylabel("bitmap_cvg (%)")
    ax.set_title("F3 — cJSON coverage over time")
    ax.legend()
    out = FIG / "F3_cjson_coverage.pdf"
    fig.tight_layout()
    fig.savefig(out)
    print(f"wrote {out}")


def f4_plateau_recovery(args):
    import pandas as pd
    import matplotlib.pyplot as plt

    src = AGG / "plateau_recovery.csv"
    if not src.exists() or src.stat().st_size == 0:
        print(f"F4: {src} missing or empty — skipping")
        return
    df = pd.read_csv(src)
    if "first_new_path_after_plateau_sec" not in df.columns:
        print("F4: missing column — skipping")
        return
    df["first_new_path_after_plateau_sec"] = pd.to_numeric(
        df["first_new_path_after_plateau_sec"], errors="coerce")
    df = df.dropna(subset=["first_new_path_after_plateau_sec"])
    if df.empty:
        print("F4: no recovery data — skipping")
        return

    FIG.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(5, 4))
    groups, labels = [], []
    for mode, sub in df.groupby("mode"):
        groups.append(sub["first_new_path_after_plateau_sec"].values)
        labels.append(mode)
    ax.boxplot(groups, labels=labels)
    ax.set_ylabel("seconds to next new path after plateau")
    ax.set_title("F4 — plateau recovery time")
    out = FIG / "F4_plateau_recovery.pdf"
    fig.tight_layout()
    fig.savefig(out)
    print(f"wrote {out}")


def f5_microbench(args):
    import pandas as pd
    import matplotlib.pyplot as plt

    src = AGG / "microbench.csv"
    if not src.exists() or src.stat().st_size == 0:
        print(f"F5: {src} missing or empty — skipping")
        return
    df = pd.read_csv(src)
    df["mean_exec_per_sec"] = pd.to_numeric(df["mean_exec_per_sec"], errors="coerce")
    df = df.dropna(subset=["mean_exec_per_sec"])
    if df.empty:
        print("F5: no microbench data — skipping")
        return

    FIG.mkdir(parents=True, exist_ok=True)
    agg = df.groupby("config")["mean_exec_per_sec"].agg(["mean", "std"]).reindex(
        ["vanilla", "fp-empty", "fp-active"])
    fig, ax = plt.subplots(figsize=(5, 4))
    ax.bar(agg.index, agg["mean"], yerr=agg["std"].fillna(0))
    ax.set_ylabel("mutator calls / sec")
    ax.set_title("F5 — mutator-only throughput")
    out = FIG / "F5_microbench.pdf"
    fig.tight_layout()
    fig.savefig(out)
    print(f"wrote {out}")


def cmd_all(args):
    f3_coverage(args)
    f4_plateau_recovery(args)
    f5_microbench(args)


def main(argv=None):
    _import_or_die()
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("f3")
    sub.add_parser("f4")
    sub.add_parser("f5")
    sub.add_parser("all")
    args = ap.parse_args(argv)
    {"f3": f3_coverage, "f4": f4_plateau_recovery, "f5": f5_microbench, "all": cmd_all}[args.cmd](args)


if __name__ == "__main__":
    main()
