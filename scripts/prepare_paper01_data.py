#!/usr/bin/env python3
"""Prepare tables for Paper 01: AI-guided recipe mutation.

This is an offline aggregation helper. It never participates in the fuzzing
runtime; it only reads completed FuzzPilot run artifacts and writes paper-facing
CSV/Markdown summaries.
"""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path
from statistics import median
from typing import Any, Iterable


PAPER_MODE_BY_ABLATION = {
    "baseline-afl": "AFL++ default",
    "rule-only": "Rule recipe",
    "full-agent": "AI recipe validated",
    "no-static-analysis": "AI recipe validated without static hints",
    "no-mutator": "Mutator disabled ablation",
}

RECIPE_SOURCE_BY_ABLATION = {
    "rule-only": "rule",
    "full-agent": "AI",
    "no-static-analysis": "AI_no_static",
}

DESIGN_REQUIRED_PAPER_MODES = [
    "AFL++ default",
    "Random recipe",
    "Rule recipe",
    "AI recipe direct",
    "AI recipe validated",
]

CSV_FLOAT_FORMAT = "{:.6g}"


@dataclass
class MicroRow:
    intervention_id: str = ""
    campaign_id: str = ""
    new_paths: int = 0
    new_edges: int = 0
    unique_crashes: int = 0
    recipe_hits: int = 0
    recipe_misses: int = 0
    reward: float = 0.0
    promoted: int = 0

    @property
    def hit_rate(self) -> float | None:
        total = self.recipe_hits + self.recipe_misses
        if total <= 0:
            return None
        return self.recipe_hits / total


@dataclass
class RunRecord:
    target: str
    mode: str
    repeat: str
    run_id: str
    run_dir: Path
    coverage_rows: list[dict[str, str]] = field(default_factory=list)
    micro_rows: list[MicroRow] = field(default_factory=list)
    agent_decisions: int = 0
    schema_invalid_decisions: int = 0
    fallback_decisions: int = 0
    run_status: str = "unknown"
    notes: list[str] = field(default_factory=list)

    @property
    def paper_mode(self) -> str:
        return PAPER_MODE_BY_ABLATION.get(self.mode, self.mode)

    @property
    def final_coverage(self) -> dict[str, str]:
        return self.coverage_rows[-1] if self.coverage_rows else {}

    @property
    def first_coverage(self) -> dict[str, str]:
        return self.coverage_rows[0] if self.coverage_rows else {}

    @property
    def telemetry_samples(self) -> int:
        return len(self.coverage_rows)

    @property
    def duration_sec(self) -> int | None:
        first_ts = as_int(self.first_coverage.get("ts"))
        last_ts = as_int(self.final_coverage.get("ts"))
        if first_ts is None or last_ts is None:
            return None
        return max(0, last_ts - first_ts)

    @property
    def recipe_hit_rate(self) -> float | None:
        hits = as_int(self.final_coverage.get("recipe_hits")) or 0
        misses = as_int(self.final_coverage.get("recipe_misses")) or 0
        total = hits + misses
        if total <= 0:
            return None
        return hits / total

    @property
    def promoted_count(self) -> int:
        return sum(1 for row in self.micro_rows if row.promoted)

    @property
    def best_reward(self) -> float | None:
        if not self.micro_rows:
            return None
        return max(row.reward for row in self.micro_rows)

    @property
    def winner_intervention_id(self) -> str:
        winners = [row for row in self.micro_rows if row.promoted]
        if not winners:
            return ""
        return max(winners, key=lambda row: row.reward).intervention_id


def as_int(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return None


def as_float(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def fmt(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return CSV_FLOAT_FORMAT.format(value)
    return str(value)


def read_coverage(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def read_sqlite(run_dir: Path, record: RunRecord) -> None:
    db_path = run_dir / "fuzzpilot.sqlite"
    if not db_path.exists():
        record.notes.append("missing fuzzpilot.sqlite")
        return

    try:
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error as exc:
        record.notes.append(f"sqlite open failed: {exc}")
        return

    try:
        record.micro_rows = [
            MicroRow(
                intervention_id=row[0] or "",
                campaign_id=row[1] or "",
                new_paths=int(row[2] or 0),
                new_edges=int(row[3] or 0),
                unique_crashes=int(row[4] or 0),
                recipe_hits=int(row[5] or 0),
                recipe_misses=int(row[6] or 0),
                reward=float(row[7] or 0.0),
                promoted=int(row[8] or 0),
            )
            for row in con.execute(
                "select intervention_id, campaign_id, new_paths, new_edges, "
                "unique_crashes, recipe_hits, recipe_misses, reward, promoted "
                "from micro_results"
            )
        ]
        decision_row = con.execute(
            "select count(*), "
            "sum(case when schema_valid = 0 then 1 else 0 end), "
            "sum(case when fallback_used != 0 then 1 else 0 end) "
            "from agent_decisions"
        ).fetchone()
        if decision_row:
            record.agent_decisions = int(decision_row[0] or 0)
            record.schema_invalid_decisions = int(decision_row[1] or 0)
            record.fallback_decisions = int(decision_row[2] or 0)
        status_row = con.execute("select status from runs limit 1").fetchone()
        if status_row and status_row[0]:
            record.run_status = str(status_row[0])
    except sqlite3.Error as exc:
        record.notes.append(f"sqlite query failed: {exc}")
    finally:
        con.close()


def parse_repeat(value: str) -> str:
    if value.startswith("r") and value[1:].isdigit():
        return value[1:]
    return value


def _read_target_from_metadata(run_dir: Path, fallback: str) -> str:
    """Recover the canonical target name (e.g. "cjson_parser") from
    run_metadata.json. Falls back to a guess parsed from the run_id if the
    metadata file is missing.
    """
    meta_path = run_dir / "run_metadata.json"
    if meta_path.exists():
        try:
            meta = json.loads(meta_path.read_text())
            name = meta.get("target_name")
            if name:
                return str(name)
        except (OSError, json.JSONDecodeError):
            pass
    return fallback


def _record_from_run_dir(run_dir: Path, target: str, mode: str, repeat: str, run_id: str) -> RunRecord:
    record = RunRecord(
        target=target,
        mode=mode,
        repeat=repeat,
        run_id=run_id,
        run_dir=run_dir,
    )
    coverage_path = run_dir / "coverage.csv"
    if coverage_path.exists():
        record.coverage_rows = read_coverage(coverage_path)
    if not record.coverage_rows:
        record.notes.append("empty coverage.csv")
    if not (run_dir / "events.jsonl").exists():
        record.notes.append("missing events.jsonl")
    if not (run_dir / "report.md").exists():
        record.notes.append("missing report.md")
    read_sqlite(run_dir, record)
    # FS status file is what scripts/paper01/run_batch.sh actually writes
    # (running | completed | failed | failed-short-run | missing-api-key |
    # skipped). Prefer it over whatever the sqlite runs table happened to
    # record, since the batch driver applies the manifest acceptance gates
    # *after* the run exits. Numeric exit code (when failed) is in the
    # sibling `exit_code` file.
    status_path = run_dir / "status"
    if status_path.exists():
        fs_status = status_path.read_text().strip()
        if fs_status:
            record.run_status = fs_status
    return record


def discover_runs(run_root: Path) -> list[RunRecord]:
    """Discover runs under either the flat or the legacy nested layout.

    Flat layout (current run_batch.sh):
        <run_root>/<run_id>/coverage.csv
        Run identity is recovered by parsing <run_id> = p1_<exp>_<target>_<mode>_r<NN>
        and reading run_metadata.json:target_name for the canonical target.

    Legacy nested layout (pre-flat, pre-Docker batch driver):
        <run_root>/<target>/<mode>/r<NN>/<run_id>/coverage.csv

    Both are accepted so older results dirs still aggregate.
    """
    records: list[RunRecord] = []
    seen_run_ids: set[str] = set()

    # Flat layout first (the current convention).
    for coverage_path in sorted(run_root.glob("*/coverage.csv")):
        run_dir = coverage_path.parent
        run_id = run_dir.name
        parts = run_id.split("_")
        if len(parts) < 5:
            continue  # not a p1_<exp>_<target>_<mode>_r<NN> id; skip
        target_guess = parts[2]
        mode = parts[3]
        repeat = parse_repeat(parts[4])
        target = _read_target_from_metadata(run_dir, target_guess)
        records.append(_record_from_run_dir(run_dir, target, mode, repeat, run_id))
        seen_run_ids.add(run_id)

    # Legacy nested layout: <root>/<target>/<mode>/r<NN>/<run_id>/coverage.csv
    for coverage_path in sorted(run_root.glob("*/*/r*/*/coverage.csv")):
        rel = coverage_path.relative_to(run_root)
        if len(rel.parts) < 5:
            continue
        target, mode, repeat_dir, run_id = rel.parts[:4]
        if run_id in seen_run_ids:
            continue
        run_dir = coverage_path.parent
        records.append(_record_from_run_dir(run_dir, target, mode, parse_repeat(repeat_dir), run_id))
        seen_run_ids.add(run_id)

    return records


def load_expected_runs(manifest_path: Path | None) -> set[tuple[str, str, str]]:
    """Parse expected (target, mode, repeat) tuples from a manifest file.

    Accepts two formats:
      1. The current paper01_preprint.yaml shape:
           experiments:
             - target: cjson_parser
               mode: baseline-afl
               runs:
                 - p1_e1_cjson_baseline-afl_r01
                 - {id: ..., mode: ...}    (E5 per-run-mode style)
      2. The legacy m6_matrix.json shape with top-level "targets".
    """
    if manifest_path is None or not manifest_path.exists():
        return set()

    expected: set[tuple[str, str, str]] = set()
    text = manifest_path.read_text()

    if manifest_path.suffix in (".yaml", ".yml"):
        try:
            import yaml  # local import: only needed for YAML manifests
        except ImportError:
            return expected
        data = yaml.safe_load(text) or {}
        for exp in data.get("experiments", []):
            if exp.get("kind") == "microbench":
                continue  # microbench is tracked separately, not in run table
            default_target = str(exp.get("target", ""))
            default_mode = str(exp.get("mode", ""))
            for run in exp.get("runs", []):
                if isinstance(run, dict):
                    run_id = str(run.get("id", ""))
                    mode = str(run.get("mode", default_mode))
                else:
                    run_id = str(run)
                    mode = default_mode
                repeat = run_id.rsplit("_r", 1)[-1] if "_r" in run_id else ""
                expected.add((default_target, mode, repeat))
        return expected

    # Legacy JSON shape.
    data = json.loads(text)
    for target_entry in data.get("targets", []):
        target = str(target_entry.get("target", ""))
        for run in target_entry.get("runs", []):
            expected.add((target, str(run.get("mode", "")), str(run.get("repeat", ""))))
    return expected


def ensure_dirs(out_dir: Path) -> dict[str, Path]:
    paths = {
        "tables": out_dir / "tables",
        "figures": out_dir / "figures",
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    for path in paths.values():
        path.mkdir(parents=True, exist_ok=True)
    return paths


def write_csv(path: Path, fieldnames: list[str], rows: Iterable[dict[str, Any]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: fmt(row.get(key)) for key in fieldnames})


def run_level_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for record in records:
        final = record.final_coverage
        rows.append(
            {
                "target": record.target,
                "implementation_mode": record.mode,
                "paper_mode": record.paper_mode,
                "repeat": record.repeat,
                "run_id": record.run_id,
                "run_dir": str(record.run_dir),
                "run_status": record.run_status,
                "telemetry_samples": record.telemetry_samples,
                "duration_sec": record.duration_sec,
                "execs_done": as_int(final.get("execs_done")),
                "execs_per_sec": as_float(final.get("execs_per_sec")),
                "paths_total": as_int(final.get("paths_total")),
                "bitmap_cvg": as_float(final.get("bitmap_cvg")),
                "unique_crashes": as_int(final.get("unique_crashes")),
                "unique_hangs": as_int(final.get("unique_hangs")),
                "recipe_hits": as_int(final.get("recipe_hits")),
                "recipe_misses": as_int(final.get("recipe_misses")),
                "recipe_hit_rate": record.recipe_hit_rate,
                "agent_decisions": record.agent_decisions,
                "schema_invalid_decisions": record.schema_invalid_decisions,
                "fallback_decisions": record.fallback_decisions,
                "micro_candidates": len(record.micro_rows),
                "promoted_count": record.promoted_count,
                "best_reward": record.best_reward,
                "winner_intervention_id": record.winner_intervention_id,
                "notes": "; ".join(record.notes),
            }
        )
    return rows


def coverage_timeseries_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for record in records:
        for index, row in enumerate(record.coverage_rows):
            rows.append(
                {
                    "target": record.target,
                    "implementation_mode": record.mode,
                    "paper_mode": record.paper_mode,
                    "repeat": record.repeat,
                    "run_id": record.run_id,
                    "sample_index": index,
                    "ts": row.get("ts", ""),
                    "execs_done": row.get("execs_done", ""),
                    "execs_per_sec": row.get("execs_per_sec", ""),
                    "paths_total": row.get("paths_total", ""),
                    "bitmap_cvg": row.get("bitmap_cvg", ""),
                    "unique_crashes": row.get("unique_crashes", ""),
                    "unique_hangs": row.get("unique_hangs", ""),
                    "recipe_hits": row.get("recipe_hits", ""),
                    "recipe_misses": row.get("recipe_misses", ""),
                }
            )
    return rows


def grouped(records: list[RunRecord], key_fn) -> dict[tuple[str, str], list[RunRecord]]:
    groups: dict[tuple[str, str], list[RunRecord]] = {}
    for record in records:
        key = key_fn(record)
        groups.setdefault(key, []).append(record)
    return groups


def numeric_values(records: list[RunRecord], field: str) -> list[float]:
    values: list[float] = []
    for record in records:
        value = as_float(record.final_coverage.get(field))
        if value is not None:
            values.append(value)
    return values


def median_or_none(values: list[float]) -> float | None:
    if not values:
        return None
    return float(median(values))


def coverage_summary_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for (target, mode), items in sorted(grouped(records, lambda r: (r.target, r.mode)).items()):
        hit_rates = [r.recipe_hit_rate for r in items if r.recipe_hit_rate is not None]
        crashes = [as_int(r.final_coverage.get("unique_crashes")) or 0 for r in items]
        rows.append(
            {
                "target": target,
                "implementation_mode": mode,
                "paper_mode": PAPER_MODE_BY_ABLATION.get(mode, mode),
                "repeats_observed": len(items),
                "median_paths_total": median_or_none(numeric_values(items, "paths_total")),
                "median_edges": "",
                "median_bitmap_cvg": median_or_none(numeric_values(items, "bitmap_cvg")),
                "unique_crashes_reported_sum": sum(crashes),
                "median_execs_per_sec": median_or_none(numeric_values(items, "execs_per_sec")),
                "median_recipe_hit_rate": median_or_none([float(v) for v in hit_rates]),
                "notes": "edges not present in current telemetry; use bitmap_cvg until edge export exists",
            }
        )
    return rows


def recipe_quality_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    groups: dict[tuple[str, str], list[MicroRow]] = {}
    for record in records:
        source = RECIPE_SOURCE_BY_ABLATION.get(record.mode)
        if source is None:
            continue
        groups.setdefault((record.target, source), []).extend(record.micro_rows)

    for (target, source), micro_rows in sorted(groups.items()):
        rewards = [row.reward for row in micro_rows]
        hit_rates = [row.hit_rate for row in micro_rows if row.hit_rate is not None]
        promoted = sum(1 for row in micro_rows if row.promoted)
        candidates = len(micro_rows)
        rows.append(
            {
                "target": target,
                "recipe_source": source,
                "candidates": candidates,
                "promoted": promoted,
                "promotion_rate": (promoted / candidates) if candidates else None,
                "median_reward": median_or_none(rewards),
                "median_hit_rate": median_or_none([float(v) for v in hit_rates]),
                "median_new_paths": median_or_none([float(row.new_paths) for row in micro_rows]),
                "median_new_edges": median_or_none([float(row.new_edges) for row in micro_rows]),
                "unique_crashes_reported_sum": sum(row.unique_crashes for row in micro_rows),
            }
        )
    return rows


def overhead_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    by_target_mode = grouped(records, lambda r: (r.target, r.mode))
    baseline_by_target: dict[str, float] = {}
    for (target, mode), items in by_target_mode.items():
        if mode == "baseline-afl":
            baseline = median_or_none(numeric_values(items, "execs_per_sec"))
            if baseline is not None and baseline > 0:
                baseline_by_target[target] = baseline

    for (target, mode), items in sorted(by_target_mode.items()):
        exec_sec = median_or_none(numeric_values(items, "execs_per_sec"))
        baseline = baseline_by_target.get(target)
        delta = None
        if exec_sec is not None and baseline is not None and baseline > 0:
            delta = (exec_sec - baseline) / baseline
        hit_rates = [r.recipe_hit_rate for r in items if r.recipe_hit_rate is not None]
        rows.append(
            {
                "target": target,
                "implementation_mode": mode,
                "paper_mode": PAPER_MODE_BY_ABLATION.get(mode, mode),
                "repeats_observed": len(items),
                "exec_sec_median": exec_sec,
                "relative_exec_sec_delta_vs_baseline": delta,
                "median_recipe_hit_rate": median_or_none([float(v) for v in hit_rates]),
                "notes": "relative throughput delta; isolate lookup overhead with a dedicated mutator benchmark before making RQ4 claims",
            }
        )
    return rows


def missing_mode_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    observed_paper_modes = {record.paper_mode for record in records}
    rows: list[dict[str, Any]] = []
    for paper_mode in DESIGN_REQUIRED_PAPER_MODES:
        if paper_mode in observed_paper_modes:
            continue
        status = "missing results"
        implementation_gap = ""
        if paper_mode == "Random recipe":
            implementation_gap = "no first-class random-recipe ablation in current CLI"
        elif paper_mode == "AI recipe direct":
            implementation_gap = "no first-class direct-promotion ablation in current CLI"
        rows.append(
            {
                "paper_mode": paper_mode,
                "status": status,
                "implementation_gap": implementation_gap,
            }
        )
    return rows


def reward_distribution_rows(records: list[RunRecord]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for record in records:
        source = RECIPE_SOURCE_BY_ABLATION.get(record.mode, "")
        for row in record.micro_rows:
            rows.append(
                {
                    "target": record.target,
                    "implementation_mode": record.mode,
                    "paper_mode": record.paper_mode,
                    "recipe_source": source,
                    "repeat": record.repeat,
                    "run_id": record.run_id,
                    "intervention_id": row.intervention_id,
                    "campaign_id": row.campaign_id,
                    "reward": row.reward,
                    "new_paths": row.new_paths,
                    "new_edges": row.new_edges,
                    "unique_crashes": row.unique_crashes,
                    "recipe_hits": row.recipe_hits,
                    "recipe_misses": row.recipe_misses,
                    "recipe_hit_rate": row.hit_rate,
                    "promoted": row.promoted,
                }
            )
    return rows


def write_markdown_table(handle, headers: list[str], rows: list[dict[str, Any]], limit: int | None = None) -> None:
    handle.write("| " + " | ".join(headers) + " |\n")
    handle.write("| " + " | ".join(["---"] * len(headers)) + " |\n")
    selected = rows if limit is None else rows[:limit]
    for row in selected:
        handle.write("| " + " | ".join(fmt(row.get(header)) for header in headers) + " |\n")
    if not selected:
        handle.write("| " + " | ".join([""] * len(headers)) + " |\n")
    handle.write("\n")


def write_paper_tables(out_dir: Path,
                       coverage_summary: list[dict[str, Any]],
                       recipe_quality: list[dict[str, Any]],
                       overhead: list[dict[str, Any]],
                       missing_modes: list[dict[str, Any]]) -> None:
    with (out_dir / "paper_tables.md").open("w") as handle:
        handle.write("# Paper 01 Data Tables\n\n")
        handle.write("Generated from real FuzzPilot artifacts only. Empty cells mean the current artifact schema does not expose that metric yet.\n\n")
        handle.write("## Main Coverage Summary\n\n")
        write_markdown_table(
            handle,
            [
                "target",
                "paper_mode",
                "implementation_mode",
                "repeats_observed",
                "median_paths_total",
                "median_edges",
                "median_bitmap_cvg",
                "unique_crashes_reported_sum",
                "notes",
            ],
            coverage_summary,
        )
        handle.write("## Recipe Quality Summary\n\n")
        write_markdown_table(
            handle,
            [
                "target",
                "recipe_source",
                "candidates",
                "promoted",
                "promotion_rate",
                "median_reward",
                "median_hit_rate",
                "median_new_paths",
                "median_new_edges",
                "unique_crashes_reported_sum",
            ],
            recipe_quality,
        )
        handle.write("## Throughput / Overhead Summary\n\n")
        write_markdown_table(
            handle,
            [
                "target",
                "paper_mode",
                "implementation_mode",
                "repeats_observed",
                "exec_sec_median",
                "relative_exec_sec_delta_vs_baseline",
                "median_recipe_hit_rate",
                "notes",
            ],
            overhead,
        )
        handle.write("## Missing Paper Modes\n\n")
        write_markdown_table(handle, ["paper_mode", "status", "implementation_gap"], missing_modes)


def write_validity_report(out_dir: Path,
                          records: list[RunRecord],
                          expected_runs: set[tuple[str, str, str]],
                          missing_modes: list[dict[str, Any]]) -> None:
    observed = {(record.target, record.mode, record.repeat) for record in records}
    missing_expected = sorted(expected_runs - observed)
    extra_observed = sorted(observed - expected_runs) if expected_runs else []
    problematic = [record for record in records if record.notes or record.run_status not in {"completed", "unknown"}]

    with (out_dir / "validity_report.md").open("w") as handle:
        handle.write("# Paper 01 Data Validity Report\n\n")
        handle.write(f"- observed runs: `{len(records)}`\n")
        if expected_runs:
            handle.write(f"- expected manifest runs: `{len(expected_runs)}`\n")
            handle.write(f"- missing expected runs: `{len(missing_expected)}`\n")
            handle.write(f"- extra observed runs: `{len(extra_observed)}`\n")
        else:
            handle.write("- expected manifest runs: `not provided`\n")
        handle.write(f"- missing required paper modes: `{len(missing_modes)}`\n")
        handle.write(f"- runs with artifact notes: `{len(problematic)}`\n\n")

        handle.write("## Missing Expected Runs\n\n")
        write_markdown_table(
            handle,
            ["target", "implementation_mode", "repeat"],
            [
                {"target": target, "implementation_mode": mode, "repeat": repeat}
                for target, mode, repeat in missing_expected
            ],
            limit=100,
        )

        handle.write("## Artifact Notes\n\n")
        write_markdown_table(
            handle,
            ["target", "implementation_mode", "repeat", "run_id", "run_status", "notes"],
            [
                {
                    "target": record.target,
                    "implementation_mode": record.mode,
                    "repeat": record.repeat,
                    "run_id": record.run_id,
                    "run_status": record.run_status,
                    "notes": "; ".join(record.notes),
                }
                for record in problematic
            ],
            limit=100,
        )

        handle.write("## Missing Paper Modes\n\n")
        write_markdown_table(handle, ["paper_mode", "status", "implementation_gap"], missing_modes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-root", default="results/paper01_ai_recipe_mutation/runs",
                        help="Directory containing run artifacts (flat <run_id>/coverage.csv "
                             "or legacy nested target/mode/rN/run_id/coverage.csv).")
    parser.add_argument("--out-dir", default="results/paper01_ai_recipe_mutation",
                        help="Output directory for paper tables.")
    parser.add_argument("--manifest", default="",
                        help="Optional m6_matrix.json manifest used to report missing runs.")
    args = parser.parse_args()

    run_root = Path(args.run_root)
    out_dir = Path(args.out_dir)
    manifest_path = Path(args.manifest) if args.manifest else None
    paths = ensure_dirs(out_dir)

    records = discover_runs(run_root) if run_root.exists() else []
    expected_runs = load_expected_runs(manifest_path)

    run_rows = run_level_rows(records)
    coverage_rows = coverage_timeseries_rows(records)
    coverage_summary = coverage_summary_rows(records)
    recipe_quality = recipe_quality_rows(records)
    overhead = overhead_rows(records)
    missing_modes = missing_mode_rows(records)
    rewards = reward_distribution_rows(records)

    write_csv(
        paths["tables"] / "run_level.csv",
        [
            "target",
            "implementation_mode",
            "paper_mode",
            "repeat",
            "run_id",
            "run_dir",
            "run_status",
            "telemetry_samples",
            "duration_sec",
            "execs_done",
            "execs_per_sec",
            "paths_total",
            "bitmap_cvg",
            "unique_crashes",
            "unique_hangs",
            "recipe_hits",
            "recipe_misses",
            "recipe_hit_rate",
            "agent_decisions",
            "schema_invalid_decisions",
            "fallback_decisions",
            "micro_candidates",
            "promoted_count",
            "best_reward",
            "winner_intervention_id",
            "notes",
        ],
        run_rows,
    )
    write_csv(
        paths["tables"] / "main_coverage_timeseries.csv",
        [
            "target",
            "implementation_mode",
            "paper_mode",
            "repeat",
            "run_id",
            "sample_index",
            "ts",
            "execs_done",
            "execs_per_sec",
            "paths_total",
            "bitmap_cvg",
            "unique_crashes",
            "unique_hangs",
            "recipe_hits",
            "recipe_misses",
        ],
        coverage_rows,
    )
    write_csv(
        paths["tables"] / "coverage_summary.csv",
        [
            "target",
            "implementation_mode",
            "paper_mode",
            "repeats_observed",
            "median_paths_total",
            "median_edges",
            "median_bitmap_cvg",
            "unique_crashes_reported_sum",
            "median_execs_per_sec",
            "median_recipe_hit_rate",
            "notes",
        ],
        coverage_summary,
    )
    write_csv(
        paths["tables"] / "recipe_quality.csv",
        [
            "target",
            "recipe_source",
            "candidates",
            "promoted",
            "promotion_rate",
            "median_reward",
            "median_hit_rate",
            "median_new_paths",
            "median_new_edges",
            "unique_crashes_reported_sum",
        ],
        recipe_quality,
    )
    write_csv(
        paths["tables"] / "overhead.csv",
        [
            "target",
            "implementation_mode",
            "paper_mode",
            "repeats_observed",
            "exec_sec_median",
            "relative_exec_sec_delta_vs_baseline",
            "median_recipe_hit_rate",
            "notes",
        ],
        overhead,
    )
    write_csv(
        paths["tables"] / "missing_paper_modes.csv",
        ["paper_mode", "status", "implementation_gap"],
        missing_modes,
    )
    write_csv(
        paths["figures"] / "reward_distribution.csv",
        [
            "target",
            "implementation_mode",
            "paper_mode",
            "recipe_source",
            "repeat",
            "run_id",
            "intervention_id",
            "campaign_id",
            "reward",
            "new_paths",
            "new_edges",
            "unique_crashes",
            "recipe_hits",
            "recipe_misses",
            "recipe_hit_rate",
            "promoted",
        ],
        rewards,
    )
    write_csv(
        paths["figures"] / "coverage_timeseries.csv",
        [
            "target",
            "implementation_mode",
            "paper_mode",
            "repeat",
            "run_id",
            "sample_index",
            "ts",
            "execs_done",
            "execs_per_sec",
            "paths_total",
            "bitmap_cvg",
            "unique_crashes",
            "unique_hangs",
            "recipe_hits",
            "recipe_misses",
        ],
        coverage_rows,
    )
    write_paper_tables(out_dir, coverage_summary, recipe_quality, overhead, missing_modes)
    write_validity_report(out_dir, records, expected_runs, missing_modes)

    print(json.dumps({
        "run_root": str(run_root),
        "out_dir": str(out_dir),
        "observed_runs": len(records),
        "expected_runs": len(expected_runs),
        "missing_paper_modes": len(missing_modes),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
