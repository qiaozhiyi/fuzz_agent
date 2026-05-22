#!/usr/bin/env python3
"""Paper 1 — aggregate per-run artifacts into paper-facing CSV / JSON.

Consumes the runs under results/paper01_ai_recipe_mutation/runs/ and emits:
  aggregated/coverage_timeseries.csv      ← F3 source
  aggregated/plateau_recovery.csv         ← F4 source
  aggregated/microbench.csv               ← F5 source
  tables/T1_per_run_summary.md            ← T1 source
  tables/T2_case_study.md (via case-study sub-command) ← T2 source

This is intentionally minimal and tolerant: missing fields produce blank
cells rather than crashing, so the script can run on partial / in-progress
batches and surface progress.

Subcommands:
  aggregate.py t1                  build T1 per-run summary
  aggregate.py timeseries          build coverage_timeseries.csv
  aggregate.py plateau-recovery    build plateau_recovery.csv
  aggregate.py microbench          build microbench.csv
  aggregate.py case-study          pick a case study from E1b and emit T2
  aggregate.py all                 run all of the above
"""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
import sys
from pathlib import Path
from statistics import median
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "results" / "paper01_ai_recipe_mutation"
RUNS = RESULTS / "runs"
MICRO = RESULTS / "microbench"
AGG = RESULTS / "aggregated"
TABLES = RESULTS / "tables"


def list_runs(prefix: str = "p1_") -> list[Path]:
    if not RUNS.exists():
        return []
    return sorted([p for p in RUNS.iterdir() if p.is_dir() and p.name.startswith(prefix)])


def parse_run_id(run_id: str) -> dict:
    # p1_e1_cjson_full-agent_r03
    parts = run_id.split("_")
    return {
        "paper": parts[0],
        "exp": parts[1],
        "target": parts[2],
        "mode": parts[3] if len(parts) > 3 else "",
        "repeat": parts[4] if len(parts) > 4 else "",
    }


def read_fuzzer_stats(stats_path: Path) -> dict:
    out: dict[str, str] = {}
    if not stats_path.exists():
        return out
    for line in stats_path.read_text().splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def locate_fuzzer_stats(run_dir: Path) -> Path:
    """Return the path to fuzzer_stats for this run.

    Newer runs have it surfaced at <run_dir>/fuzzer_stats (copied by run_batch.sh).
    Older runs (or runs from before the copy path was fixed) only have it at
    <run_dir>/work/run_*/main_out/default/fuzzer_stats. Prefer the surfaced
    copy when present; fall back to the nested location otherwise.
    """
    surfaced = run_dir / "fuzzer_stats"
    if surfaced.exists():
        return surfaced
    work = run_dir / "work"
    if work.is_dir():
        for inner in sorted(work.glob("run_*")):
            candidate = inner / "main_out" / "default" / "fuzzer_stats"
            if candidate.exists():
                return candidate
    return surfaced  # returns a non-existent path; read_fuzzer_stats() handles that


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    rows = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


# ---------------------------------------------------------------------------
def cmd_t1(args):
    AGG.mkdir(parents=True, exist_ok=True)
    TABLES.mkdir(parents=True, exist_ok=True)

    headers = [
        "run_id", "exp", "target", "mode", "repeat",
        "status", "execs_done", "execs_per_sec",
        "paths_total", "edges_found", "bitmap_cvg",
        "unique_crashes", "unique_hangs",
        "plateau_events", "proposals", "promotions",
        "schema_valid_rate", "fallback_rate",
    ]
    out_csv = AGG / "t1_per_run_summary.csv"
    out_md = TABLES / "T1_per_run_summary.md"

    rows = []
    for run_dir in list_runs():
        info = parse_run_id(run_dir.name)
        if info["exp"] not in ("e1", "e2"):  # T1 covers E1 + E2 only
            continue
        status_file = run_dir / "status"
        status = status_file.read_text().strip() if status_file.exists() else "unknown"

        stats = read_fuzzer_stats(locate_fuzzer_stats(run_dir))
        decisions = read_jsonl(run_dir / "agent_decisions.jsonl")
        events = read_jsonl(run_dir / "events.jsonl")

        plateau_events = sum(1 for e in events if e.get("event") == "plateau_detected")
        proposals = len(decisions)
        promotions = sum(1 for e in events if e.get("event") == "promotion")
        schema_valid = [d.get("schema_valid", False) for d in decisions]
        schema_valid_rate = (sum(schema_valid) / len(schema_valid)) if schema_valid else ""
        fallback = sum(1 for d in decisions if d.get("fallback"))
        fallback_rate = (fallback / len(decisions)) if decisions else ""

        rows.append({
            "run_id": run_dir.name,
            "exp": info["exp"],
            "target": info["target"],
            "mode": info["mode"],
            "repeat": info["repeat"],
            "status": status,
            "execs_done": stats.get("execs_done", ""),
            "execs_per_sec": stats.get("execs_per_sec", ""),
            "paths_total": stats.get("paths_total", stats.get("corpus_count", "")),
            "edges_found": stats.get("edges_found", ""),
            "bitmap_cvg": stats.get("bitmap_cvg", ""),
            "unique_crashes": stats.get("unique_crashes", stats.get("saved_crashes", "")),
            "unique_hangs": stats.get("unique_hangs", stats.get("saved_hangs", "")),
            "plateau_events": plateau_events,
            "proposals": proposals,
            "promotions": promotions,
            "schema_valid_rate": schema_valid_rate,
            "fallback_rate": fallback_rate,
        })

    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=headers)
        w.writeheader()
        w.writerows(rows)

    with out_md.open("w") as f:
        f.write("# T1 — Per-run summary (Paper 1)\n\n")
        f.write("| " + " | ".join(headers) + " |\n")
        f.write("|" + "|".join(["---"] * len(headers)) + "|\n")
        for r in rows:
            f.write("| " + " | ".join(str(r[h]) for h in headers) + " |\n")
        if not rows:
            f.write("\n_No runs found. Run scripts/paper01/run_batch.sh first._\n")

    print(f"wrote {out_csv} ({len(rows)} rows)")
    print(f"wrote {out_md}")


# ---------------------------------------------------------------------------
def cmd_timeseries(args):
    AGG.mkdir(parents=True, exist_ok=True)
    out = AGG / "coverage_timeseries.csv"
    headers = ["run_id", "exp", "mode", "repeat", "ts", "execs_done",
               "paths_total", "edges_found", "bitmap_cvg"]
    n = 0
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(headers)
        for run_dir in list_runs():
            info = parse_run_id(run_dir.name)
            cov = run_dir / "coverage.csv"
            if not cov.exists():
                continue
            with cov.open() as cf:
                reader = csv.DictReader(cf)
                for row in reader:
                    w.writerow([
                        run_dir.name, info["exp"], info["mode"], info["repeat"],
                        row.get("ts", ""),
                        row.get("execs_done", ""),
                        row.get("paths_total", ""),
                        row.get("edges_found", ""),
                        row.get("bitmap_cvg", ""),
                    ])
                    n += 1
    print(f"wrote {out} ({n} rows)")


# ---------------------------------------------------------------------------
def cmd_plateau_recovery(args):
    AGG.mkdir(parents=True, exist_ok=True)
    out = AGG / "plateau_recovery.csv"
    headers = ["run_id", "exp", "mode", "repeat",
               "plateau_ts", "promoted_ts", "first_new_path_after_plateau_sec"]
    n = 0
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(headers)
        for run_dir in list_runs():
            info = parse_run_id(run_dir.name)
            events = read_jsonl(run_dir / "events.jsonl")
            plateau_ts = next((e.get("ts") for e in events if e.get("event") == "plateau_detected"), "")
            promoted_ts = next((e.get("ts") for e in events if e.get("event") == "promotion"), "")
            # First "new_path" telemetry event strictly after plateau_ts
            try:
                pts = float(plateau_ts) if plateau_ts else None
            except (TypeError, ValueError):
                pts = None
            first_new = ""
            if pts is not None:
                for e in events:
                    if e.get("event") == "telemetry" and float(e.get("ts", 0)) > pts:
                        # heuristic: paths_total or edges_found increased
                        # Aggregator does not know prior values; just report the timestamp delta
                        first_new = float(e["ts"]) - pts
                        break
            w.writerow([
                run_dir.name, info["exp"], info["mode"], info["repeat"],
                plateau_ts, promoted_ts, first_new,
            ])
            n += 1
    print(f"wrote {out} ({n} rows)")


# ---------------------------------------------------------------------------
def cmd_microbench(args):
    AGG.mkdir(parents=True, exist_ok=True)
    out = AGG / "microbench.csv"
    headers = ["run_id", "config", "repeat", "iterations",
               "mean_ns_per_call", "mean_exec_per_sec", "stddev"]
    n = 0
    with out.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(headers)
        if MICRO.exists():
            for jp in sorted(MICRO.glob("*.json")):
                try:
                    data = json.loads(jp.read_text())
                except Exception:
                    continue
                parts = jp.stem.split("_")
                cfg = parts[3] if len(parts) > 3 else ""
                rep = parts[4] if len(parts) > 4 else ""
                w.writerow([
                    jp.stem, cfg, rep,
                    data.get("iterations", ""),
                    data.get("mean_ns_per_call", ""),
                    data.get("mean_exec_per_sec", ""),
                    data.get("stddev", ""),
                ])
                n += 1
    print(f"wrote {out} ({n} rows)")


# ---------------------------------------------------------------------------
def cmd_throughput_parity(args):
    """End-to-end exec/sec comparison: E1b (full-agent) vs E1a (baseline-afl).

    This is the canonical evidence for the paper's RQ2 throughput claim --
    the microbench (F5) measures dispatch cost in isolation, but reviewers
    care about whether AFL++ actually runs slower when it uses FuzzPilot's
    mutator. We answer that here by comparing fuzzer_stats:execs_per_sec
    medians across the 5 baseline-afl runs and 5 full-agent runs from E1.

    Output:
      aggregated/throughput_parity.csv  — per-mode median + per-run rows
      tables/throughput_parity.md       — short markdown with ratio + gate
    """
    AGG.mkdir(parents=True, exist_ok=True)
    TABLES.mkdir(parents=True, exist_ok=True)
    out_csv = AGG / "throughput_parity.csv"
    out_md = TABLES / "throughput_parity.md"

    per_mode: dict[str, list[float]] = {"baseline-afl": [], "full-agent": []}
    per_run_rows = []
    for run_dir in list_runs("p1_e1_cjson_"):
        info = parse_run_id(run_dir.name)
        mode = info["mode"]
        if mode not in per_mode:
            continue
        stats = read_fuzzer_stats(locate_fuzzer_stats(run_dir))
        try:
            eps = float(stats.get("execs_per_sec", ""))
        except (TypeError, ValueError):
            eps = None
        per_run_rows.append({"run_id": run_dir.name, "mode": mode, "execs_per_sec": eps})
        if eps is not None:
            per_mode[mode].append(eps)

    headers = ["run_id", "mode", "execs_per_sec"]
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=headers)
        w.writeheader()
        w.writerows(per_run_rows)

    baseline = per_mode["baseline-afl"]
    full = per_mode["full-agent"]
    baseline_med = median(baseline) if baseline else None
    full_med = median(full) if full else None
    ratio = (full_med / baseline_med) if (baseline_med and full_med) else None

    with out_md.open("w") as f:
        f.write("# End-to-end throughput parity (Paper 1, §6 RQ2)\n\n")
        f.write("Compares median AFL++ exec/sec between baseline-afl (E1a) and ")
        f.write("full-agent (E1b). The canonical RQ2 number for the paper.\n\n")
        f.write("| Mode | n runs | Median exec/sec |\n|---|---:|---:|\n")
        f.write(f"| baseline-afl | {len(baseline)} | {baseline_med if baseline_med is not None else 'n/a'} |\n")
        f.write(f"| full-agent | {len(full)} | {full_med if full_med is not None else 'n/a'} |\n\n")
        if ratio is not None:
            gate = 0.85  # manifest:end_to_end_full_agent_vs_baseline_min_ratio
            verdict = "PASS" if ratio >= gate else "FAIL"
            f.write(f"**Ratio (full-agent / baseline-afl): {ratio:.3f}**  ")
            f.write(f"(gate ≥ {gate}: {verdict})\n\n")
            if ratio < gate:
                f.write("> FuzzPilot mutator is dragging AFL++ end-to-end exec/sec ")
                f.write("below the throughput-parity gate. Investigate mutator hot ")
                f.write("path before submitting; this is the real RQ2 number.\n")
        else:
            f.write("_Insufficient data to compute ratio (need both baseline-afl and full-agent runs)._\n")

    print(f"wrote {out_csv} ({len(per_run_rows)} rows)")
    print(f"wrote {out_md}")
    if ratio is not None:
        print(f"throughput parity ratio: {ratio:.3f}")


# ---------------------------------------------------------------------------
def cmd_case_study(args):
    TABLES.mkdir(parents=True, exist_ok=True)
    candidates = []
    for run_dir in list_runs("p1_e1_cjson_full-agent_"):
        events = read_jsonl(run_dir / "events.jsonl")
        decisions = read_jsonl(run_dir / "agent_decisions.jsonl")
        plateau_ts = next((float(e["ts"]) for e in events if e.get("event") == "plateau_detected" and e.get("ts")), None)
        if plateau_ts is None:
            continue
        valid = next((d for d in decisions if d.get("schema_valid")), None)
        if not valid:
            continue
        promotion = next((e for e in events if e.get("event") == "promotion" and float(e.get("ts", 0)) > plateau_ts), None)
        if not promotion:
            continue
        # heuristic 10-min new path check: any telemetry within 600s after promotion
        new_path = any(
            e.get("event") == "telemetry"
            and float(e.get("ts", 0)) - float(promotion["ts"]) < 600
            for e in events
        )
        if not new_path:
            continue
        candidates.append((plateau_ts, run_dir.name))

    out_json = TABLES.parent / "tables" / "T2_case_study.json"
    out_md = TABLES / "T2_case_study.md"

    if not candidates:
        out_json.write_text(json.dumps({"selected": None, "reason": "no candidate satisfied criteria"}, indent=2))
        out_md.write_text("# T2 Case Study\n\n_No qualifying case study found. Increase E1b repeats or tune plateau sensitivity._\n")
        print("no case study candidate found")
        return

    candidates.sort()
    _, chosen = candidates[0]
    out_json.write_text(json.dumps({"selected": chosen, "candidates": [c[1] for c in candidates]}, indent=2))
    out_md.write_text(f"# T2 Case Study\n\nSelected run: `{chosen}`\n\n"
                      f"Other qualifying runs: {[c[1] for c in candidates[1:]]}\n\n"
                      "Inspect `events.jsonl`, `agent_decisions.jsonl`, and `main_recipes/` "
                      "in the selected run directory to populate the case-study table.\n")
    print(f"selected case study: {chosen}")


# ---------------------------------------------------------------------------
def cmd_all(args):
    cmd_t1(args)
    cmd_timeseries(args)
    cmd_plateau_recovery(args)
    cmd_microbench(args)
    cmd_throughput_parity(args)
    cmd_case_study(args)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("t1")
    sub.add_parser("timeseries")
    sub.add_parser("plateau-recovery")
    sub.add_parser("microbench")
    sub.add_parser("throughput-parity")
    sub.add_parser("case-study")
    sub.add_parser("all")
    args = ap.parse_args(argv)

    {
        "t1": cmd_t1,
        "timeseries": cmd_timeseries,
        "plateau-recovery": cmd_plateau_recovery,
        "microbench": cmd_microbench,
        "throughput-parity": cmd_throughput_parity,
        "case-study": cmd_case_study,
        "all": cmd_all,
    }[args.cmd](args)


if __name__ == "__main__":
    main()
