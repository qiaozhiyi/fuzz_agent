#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import re
import random
import sys
from collections import defaultdict
from statistics import median
from pathlib import Path


LLM_MODES = {
    "full-agent",
    "ai-direct",
    "no-static-analysis",
    "no-semantic-context",
    "single-agent-coordinator",
    "single-agent-dictionary",
    "no-mutator",
}


def count_events(path: Path, needle: str) -> int:
    if not path.exists():
        return 0
    return sum(1 for line in path.read_text(errors="replace").splitlines() if needle in line)


def iter_json_objects(text: str):
    start = None
    depth = 0
    in_string = False
    escape = False
    for index, char in enumerate(text):
        if start is None:
            if char in "{[":
                start = index
                depth = 1
                in_string = False
                escape = False
            continue
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char in "{[":
            depth += 1
        elif char in "}]":
            depth -= 1
            if depth == 0:
                yield text[start:index + 1]
                start = None


def load_json_objects(path: Path) -> list[dict[str, object]]:
    if not path.exists():
        return []
    objects = []
    for raw in iter_json_objects(path.read_text(errors="replace")):
        try:
            obj = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            objects.append(obj)
    return objects


def decision_stats(path: Path) -> dict[str, object]:
    stats = {
        "total": 0,
        "valid": 0,
        "auth_errors": 0,
        "fallbacks": 0,
        "input_tokens": 0,
        "output_tokens": 0,
        "latencies_ms": [],
    }
    for obj in load_json_objects(path):
        stats["total"] += 1
        response = obj.get("model_response") if isinstance(obj, dict) else {}
        schema_valid = obj.get("schema_valid")
        error_kind = obj.get("error_kind")
        if isinstance(response, dict):
            schema_valid = response.get("schema_valid", schema_valid)
            error_kind = response.get("error_kind", error_kind)
        if schema_valid is True:
            stats["valid"] += 1
        if error_kind == "auth_error":
            stats["auth_errors"] += 1
        if obj.get("fallback_used") is True:
            stats["fallbacks"] += 1
        stats["input_tokens"] += int(obj.get("input_tokens") or 0)
        stats["output_tokens"] += int(obj.get("output_tokens") or 0)
        latency = parse_float(str(obj.get("latency_ms", "")))
        if latency is not None:
            stats["latencies_ms"].append(latency)
    return stats


def fuzzer_stat(path: Path, key: str) -> str:
    if not path.exists():
        return ""
    prefix = key + ":"
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith(prefix):
            return line.split(":", 1)[1].strip()
    return ""


def parse_float(value: str) -> float | None:
    if value == "":
        return None
    try:
        return float(value.rstrip("%"))
    except ValueError:
        return None


def numeric(row: dict[str, object], key: str) -> float | None:
    return parse_float(str(row.get(key, "")))


def percentile(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    rank = (len(sorted_values) - 1) * p
    low = int(rank)
    high = min(low + 1, len(sorted_values) - 1)
    frac = rank - low
    return sorted_values[low] * (1.0 - frac) + sorted_values[high] * frac


def median_iqr_text(values: list[float], digits: int = 2) -> str:
    clean = sorted(value for value in values if value is not None)
    if not clean:
        return "n/a"
    med = median(clean)
    q1 = percentile(clean, 0.25)
    q3 = percentile(clean, 0.75)
    return f"{med:.{digits}f} [{q1:.{digits}f}, {q3:.{digits}f}]"


def numeric_text(value: float | None, digits: int = 2) -> str:
    if value is None:
        return ""
    return f"{value:.{digits}f}"


def deterministic_bootstrap_median_ci(values: list[float],
                                      label: str,
                                      reps: int = 2000) -> tuple[float, float] | None:
    clean = [value for value in values if value is not None]
    if not clean:
        return None
    if len(clean) == 1:
        return clean[0], clean[0]
    seed = int(hashlib.sha256(label.encode("utf-8")).hexdigest()[:16], 16)
    rng = random.Random(seed)
    boot = []
    for _ in range(reps):
        sample = [clean[rng.randrange(len(clean))] for _ in clean]
        boot.append(median(sample))
    boot.sort()
    return percentile(boot, 0.025), percentile(boot, 0.975)


def ratio_ci_text(values: list[float], label: str, digits: int = 3) -> str:
    clean = [value for value in values if value is not None]
    if not clean:
        return "n/a"
    med = median(clean)
    ci = deterministic_bootstrap_median_ci(clean, label)
    if ci is None:
        return "n/a"
    low, high = ci
    return f"{med:.{digits}f} [{low:.{digits}f}, {high:.{digits}f}]"


def paired_ratios(rows: list[dict[str, object]],
                  numerator_mode: str,
                  denominator_mode: str,
                  metric: str) -> list[float]:
    by_key: dict[tuple[str, str, str], float] = {}
    for row in completed(rows):
        value = numeric(row, metric)
        if value is None:
            continue
        key = (
            str(row.get("target", "")),
            str(row.get("repeat", "")),
            str(row.get("mode", "")),
        )
        by_key[key] = value

    ratios = []
    pairs = sorted({
        (target, repeat)
        for target, repeat, mode in by_key
        if mode in {numerator_mode, denominator_mode}
    })
    for target, repeat in pairs:
        numerator = by_key.get((target, repeat, numerator_mode))
        denominator = by_key.get((target, repeat, denominator_mode))
        if numerator is None or denominator is None or denominator <= 0:
            continue
        ratios.append(numerator / denominator)
    return ratios


def win_rate_text(ratios: list[float]) -> str:
    if not ratios:
        return "n/a"
    wins = sum(1 for ratio in ratios if ratio > 1.0)
    ties = sum(1 for ratio in ratios if ratio == 1.0)
    return f"{wins}/{len(ratios)} wins, {ties} ties"


def load_events(path: Path) -> list[dict[str, object]]:
    if not path.exists():
        return []
    events = []
    for line in path.read_text(errors="replace").splitlines():
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            events.append(obj)
    return events


def load_coverage_rows(path: Path) -> list[dict[str, float]]:
    if not path.exists():
        return []
    rows = []
    with path.open("r", encoding="utf-8", newline="") as inp:
        reader = csv.DictReader(inp)
        for row in reader:
            parsed = {}
            for key, value in row.items():
                parsed_value = parse_float(value or "")
                if parsed_value is not None:
                    parsed[key] = parsed_value
            if "ts" in parsed:
                rows.append(parsed)
    return rows


def first_promotion_ts(events: list[dict[str, object]]) -> float | None:
    for event in events:
        if event.get("event") not in {"promotion", "ai_direct_promoted"}:
            continue
        ts = parse_float(str(event.get("ts", "")))
        if ts is not None:
            return ts
    return None


def post_promotion_gain(coverage_rows: list[dict[str, float]],
                        promotion_ts: float | None,
                        metric: str,
                        window_sec: int) -> float | None:
    if promotion_ts is None or not coverage_rows:
        return None
    before = [
        row for row in coverage_rows
        if row.get("ts") is not None and row["ts"] <= promotion_ts and metric in row
    ]
    after = [
        row for row in coverage_rows
        if row.get("ts") is not None
        and promotion_ts < row["ts"] <= promotion_ts + window_sec
        and metric in row
    ]
    if not after:
        return None
    baseline = before[-1][metric] if before else after[0][metric]
    return max(row[metric] for row in after) - baseline


def load_manifest(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        import yaml
    except ImportError:
        return {}
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def load_noise_summary(path: Path | None) -> dict[str, object]:
    if path is None or not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def collect_rows(root: Path) -> list[dict[str, object]]:
    rows = []
    for run_dir in sorted(root.glob("*")) if root.exists() else []:
        if not run_dir.is_dir():
            continue
        meta_path = run_dir / "runner_metadata.json"
        if not meta_path.exists():
            continue
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        decision = decision_stats(run_dir / "agent_decisions.jsonl")
        decisions = int(decision["total"])
        schema_valid = int(decision["valid"])
        auth_errors = int(decision["auth_errors"])
        latencies_ms = list(decision["latencies_ms"])
        events = load_events(run_dir / "events.jsonl")
        coverage_rows = load_coverage_rows(run_dir / "coverage.csv")
        promotion_ts = first_promotion_ts(events)
        rows.append({
            "run_id": meta.get("run_id", run_dir.name),
            "stage": meta.get("stage", ""),
            "target": meta.get("target", ""),
            "mode": meta.get("mode", ""),
            "repeat": meta.get("repeat", ""),
            "run_order_index": meta.get("run_order_index", ""),
            "blocked_order_index": meta.get("blocked_order_index", ""),
            "cpu": meta.get("cpu", ""),
            "budget_sec": meta.get("budget_sec", ""),
            "status": (run_dir / "status").read_text().strip() if (run_dir / "status").exists() else "",
            "exit_code": (run_dir / "exit_code").read_text().strip() if (run_dir / "exit_code").exists() else "",
            "run_time": fuzzer_stat(run_dir / "fuzzer_stats", "run_time"),
            "execs_per_sec": fuzzer_stat(run_dir / "fuzzer_stats", "execs_per_sec"),
            "paths_total": fuzzer_stat(run_dir / "fuzzer_stats", "paths_total"),
            "bitmap_cvg": fuzzer_stat(run_dir / "fuzzer_stats", "bitmap_cvg"),
            "agent_decisions": decisions,
            "schema_valid_rate": f"{schema_valid / decisions:.6f}" if decisions else "",
            "auth_errors": auth_errors,
            "fallback_rate": f"{int(decision['fallbacks']) / decisions:.6f}" if decisions else "",
            "llm_input_tokens": int(decision["input_tokens"]),
            "llm_output_tokens": int(decision["output_tokens"]),
            "llm_latency_ms_median": numeric_text(median(latencies_ms) if latencies_ms else None, 0),
            "llm_latency_ms_p95": numeric_text(
                percentile(sorted(latencies_ms), 0.95) if latencies_ms else None, 0),
            "plateau_events": count_events(run_dir / "events.jsonl", '"event":"plateau_detected"'),
            "validation_decisions": count_events(run_dir / "events.jsonl", '"event":"winner_decided"'),
            "promotions": count_events(run_dir / "events.jsonl", '"event":"promotion"'),
            "ai_direct_promotions": count_events(run_dir / "events.jsonl", '"event":"ai_direct_promoted"'),
            "post_promotion_paths_10m": numeric_text(
                post_promotion_gain(coverage_rows, promotion_ts, "paths_total", 600), 0),
            "post_promotion_edges_10m": numeric_text(
                post_promotion_gain(coverage_rows, promotion_ts, "edges_found", 600), 0),
            "post_promotion_paths_30m": numeric_text(
                post_promotion_gain(coverage_rows, promotion_ts, "paths_total", 1800), 0),
            "post_promotion_edges_30m": numeric_text(
                post_promotion_gain(coverage_rows, promotion_ts, "edges_found", 1800), 0),
            "_metadata": meta,
            "_run_dir": run_dir,
        })
    return rows


FIELDS = [
    "run_id", "stage", "target", "mode", "repeat", "run_order_index",
    "blocked_order_index", "cpu", "budget_sec",
    "status", "exit_code", "run_time", "execs_per_sec", "paths_total",
    "bitmap_cvg", "agent_decisions", "schema_valid_rate", "auth_errors", "fallback_rate",
    "llm_input_tokens", "llm_output_tokens", "llm_latency_ms_median", "llm_latency_ms_p95",
    "plateau_events", "validation_decisions", "promotions", "ai_direct_promotions",
    "post_promotion_paths_10m", "post_promotion_edges_10m",
    "post_promotion_paths_30m", "post_promotion_edges_30m",
]


def write_csv(rows: list[dict[str, object]], out) -> None:
    public_rows = [{field: row.get(field, "") for field in FIELDS} for row in rows]
    writer = csv.DictWriter(out, fieldnames=FIELDS)
    writer.writeheader()
    writer.writerows(public_rows)


def gate_line(ok: bool, name: str, detail: str) -> str:
    return f"- [{'PASS' if ok else 'FAIL'}] {name}: {detail}"


def scan_forbidden(root: Path, patterns: list[str]) -> list[str]:
    compiled = []
    for pattern in patterns:
        try:
            compiled.append(re.compile(pattern.replace("[[:space:]]", r"\s")))
        except re.error:
            continue
    if not compiled or not root.exists():
        return []

    hits = []
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix in {".sqlite", ".db"}:
            continue
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        for pattern in compiled:
            if pattern.search(text):
                hits.append(str(path))
                break
    return hits


def completed(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    return [row for row in rows if row.get("status") == "completed" and str(row.get("exit_code")) in {"", "0"}]


def stage_modes(manifest: dict, stage_name: str) -> list[str]:
    stages = manifest.get("stages") or {}
    stage = stages.get(stage_name) or {}
    return list(stage.get("modes") or manifest.get("modes") or [])


def expected_run_ids(manifest: dict, stage_names: list[str]) -> list[str]:
    expected = []
    stages = manifest.get("stages") or {}
    targets = manifest.get("targets") or []
    for stage_name in stage_names:
        stage = stages.get(stage_name) or {}
        modes = stage_modes(manifest, stage_name)
        repeats = int(stage.get("repeats") or 0)
        for repeat in range(1, repeats + 1):
            for target in targets:
                target_id = target.get("id", "")
                for mode in modes:
                    expected.append(
                        f"arch_{stage_name.lower()}_{target_id}_{mode}_r{repeat:02d}"
                    )
    return expected


def report_stage_names(rows: list[dict[str, object]],
                       manifest: dict,
                       requested_stage: str) -> list[str]:
    stages = manifest.get("stages") or {}
    if requested_stage:
        return [requested_stage]
    discovered = sorted({str(row.get("stage", "")) for row in rows if row.get("stage")})
    return [stage for stage in discovered if stage in stages]


def acceptance_profile(manifest: dict, stage_names: list[str]) -> str:
    stages = manifest.get("stages") or {}
    profiles = {
        str((stages.get(stage_name) or {}).get("acceptance_profile", "paper"))
        for stage_name in stage_names
    }
    if profiles == {"infrastructure"}:
        return "infrastructure"
    return "paper"


def generate_report(rows: list[dict[str, object]],
                    manifest: dict,
                    root: Path,
                    requested_stage: str = "",
                    noise_summary: dict[str, object] | None = None,
                    stage_plan: dict[str, object] | None = None,
                    manifest_sha256: str = "") -> str:
    acceptance = manifest.get("acceptance") or {}
    min_schema = float(acceptance.get("min_schema_valid_rate", 0.8))
    min_throughput = float(acceptance.get("min_full_agent_throughput_ratio", 0.70))
    min_decisions = int(acceptance.get("min_full_agent_decisions", 1))
    min_plateau = int(acceptance.get("min_full_agent_plateau_events", 1))
    forbidden = list(acceptance.get("forbidden_secret_patterns") or [])

    stage_names = report_stage_names(rows, manifest, requested_stage)
    profile = acceptance_profile(manifest, stage_names) if stage_names else "paper"
    if stage_names:
        rows_for_gates = [row for row in rows if str(row.get("stage", "")) in stage_names]
    else:
        rows_for_gates = rows

    done = completed(rows_for_gates)
    status_counts: dict[str, int] = defaultdict(int)
    for row in rows_for_gates:
      status_counts[str(row.get("status", ""))] += 1

    lines = [
        "# FuzzPilot Architecture Pilot Acceptance Report",
        "",
        f"- Root: `{root}`",
        f"- Stages checked: {stage_names if stage_names else 'discovered rows only'}",
        f"- Acceptance profile: `{profile}`",
        f"- Runs discovered: {len(rows_for_gates)}",
        f"- Completed runs: {len(done)}",
        f"- Status counts: {dict(sorted(status_counts.items()))}",
        "",
        "## Gates",
    ]

    expected_ids = expected_run_ids(manifest, stage_names) if stage_names else []
    expected_id_set = set(expected_ids)
    discovered_ids = {str(row.get("run_id", "")) for row in rows_for_gates}
    missing_ids = [run_id for run_id in expected_ids if run_id not in discovered_ids]
    extra_ids = [
        str(row.get("run_id", "")) for row in rows_for_gates
        if expected_ids and str(row.get("run_id", "")) not in expected_id_set
    ]
    lines.append(gate_line(
        bool(expected_ids) and not missing_ids and not extra_ids,
        "manifest matrix completeness",
        f"{len(discovered_ids)}/{len(expected_ids)} expected cells discovered"
        if not missing_ids and not extra_ids else
        "missing=" + ",".join(missing_ids[:8]) +
        (" extra=" + ",".join(extra_ids[:8]) if extra_ids else ""),
    ))

    plan = stage_plan or {}
    plan_stage_config = plan.get("stage_config") if isinstance(plan.get("stage_config"), dict) else {}
    plan_resource = plan.get("resource_plan") if isinstance(plan.get("resource_plan"), dict) else {}
    plan_manifest = plan.get("manifest") if isinstance(plan.get("manifest"), dict) else {}
    plan_expected_ids = list(plan_stage_config.get("expected_run_ids") or [])
    plan_fuzz_slots = list(plan_resource.get("fuzz_slots") or [])
    plan_errors = []
    if not plan:
        plan_errors.append("missing stage plan")
    if plan and len(stage_names) == 1 and plan.get("stage") != stage_names[0]:
        plan_errors.append(f"stage={plan.get('stage')} expected={stage_names[0]}")
    if plan and manifest_sha256 and plan_manifest.get("sha256") != manifest_sha256:
        plan_errors.append("manifest sha256 mismatch")
    if plan and plan_expected_ids != expected_ids:
        plan_errors.append(
            f"expected_run_ids={len(plan_expected_ids)} expected={len(expected_ids)}"
        )
    if plan and int(plan_resource.get("total_cells") or -1) != len(expected_ids):
        plan_errors.append(
            f"total_cells={plan_resource.get('total_cells')} expected={len(expected_ids)}"
        )
    if plan and int(plan_resource.get("llm_parallel") or 0) != 1:
        plan_errors.append(f"llm_parallel={plan_resource.get('llm_parallel')}")
    if plan and int(plan_resource.get("non_llm_parallel") or 0) > len(plan_fuzz_slots):
        plan_errors.append(
            f"non_llm_parallel={plan_resource.get('non_llm_parallel')} "
            f"fuzz_slots={len(plan_fuzz_slots)}"
        )
    plan_detail = "stage plan matches manifest matrix and resource policy"
    if plan_errors:
        plan_detail = ", ".join(plan_errors[:8])
    elif plan:
        plan_detail = (
            f"manifest_sha256={str(plan_manifest.get('sha256', ''))[:12]}, "
            f"cells={plan_resource.get('total_cells')}, "
            f"llm_parallel={plan_resource.get('llm_parallel')}, "
            f"non_llm_parallel={plan_resource.get('non_llm_parallel')}, "
            f"fuzz_slots={plan_fuzz_slots}"
        )
    lines.append(gate_line(
        bool(plan) and not plan_errors,
        "stage plan",
        plan_detail,
    ))

    plan_policy = plan.get("policy") if isinstance(plan.get("policy"), dict) else {}
    policy_errors = []
    if not plan:
        policy_errors.append("missing stage plan")
    elif profile != "infrastructure":
        if plan.get("dry_run") is not False:
            policy_errors.append("dry_run=true")
        if plan_policy.get("require_api_key") is not True:
            policy_errors.append("require_api_key=false")
        if plan_policy.get("fail_on_gate_fail") is not True:
            policy_errors.append("fail_on_gate_fail=false")
        if stage_names and any(stage_name in {"B", "C"} for stage_name in stage_names):
            if plan_policy.get("require_previous_stage_pass") is not True:
                policy_errors.append("require_previous_stage_pass=false")
    policy_detail = "paper-profile execution policy enforced"
    if profile == "infrastructure" and plan:
        policy_detail = (
            "infrastructure profile; policy recorded as "
            f"dry_run={plan.get('dry_run')}, "
            f"require_api_key={plan_policy.get('require_api_key')}, "
            f"fail_on_gate_fail={plan_policy.get('fail_on_gate_fail')}"
        )
    elif policy_errors:
        policy_detail = ", ".join(policy_errors[:8])
    lines.append(gate_line(
        bool(plan) and not policy_errors,
        "stage execution policy",
        policy_detail,
    ))

    incomplete_expected = [
        f"{row.get('run_id')}:{row.get('status') or 'missing-status'}"
        for row in rows_for_gates
        if expected_id_set and str(row.get("run_id", "")) in expected_id_set and
        row.get("status") != "completed"
    ]
    lines.append(gate_line(
        bool(expected_ids) and not missing_ids and not incomplete_expected,
        "manifest matrix completion",
        "all expected cells completed"
        if not incomplete_expected and not missing_ids else
        "incomplete=" + ",".join((missing_ids + incomplete_expected)[:12]),
    ))

    complete_artifacts_missing = []
    for row in done:
        run_dir = row["_run_dir"]
        for name in ["coverage.csv", "events.jsonl", "report.md", "runner_metadata.json"]:
            if not (run_dir / name).exists():
                complete_artifacts_missing.append(f"{row['run_id']}:{name}")
    lines.append(gate_line(
        not complete_artifacts_missing,
        "completed-run artifacts",
        "all completed runs have coverage/events/report/metadata"
        if not complete_artifacts_missing else ", ".join(complete_artifacts_missing[:12]),
    ))

    noise = noise_summary or {}
    noise_warnings = noise.get("warnings") if isinstance(noise.get("warnings"), list) else []
    noise_samples = int(noise.get("samples") or 0) if noise else 0
    noise_detail = "missing runtime noise summary"
    if noise:
        noise_detail = (
            f"samples={noise_samples}, "
            f"max_load1_per_cpu={float(noise.get('max_load1_per_cpu') or 0.0):.3f}, "
            f"min_mem_available_kib={noise.get('min_mem_available_kib', '')}, "
            f"max_swap_used_kib={noise.get('max_swap_used_kib', '')}"
        )
        if noise_warnings:
            noise_detail += "; warnings=" + ", ".join(str(item) for item in noise_warnings[:6])
    lines.append(gate_line(
        bool(noise) and noise_samples > 0 and not noise_warnings,
        "runtime noise summary",
        noise_detail,
    ))

    llm_rows = [row for row in done if row.get("mode") in LLM_MODES]
    llm_auth_errors = sum(int(row.get("auth_errors") or 0) for row in llm_rows)
    skipped_llm = [
        row for row in rows_for_gates
        if row.get("mode") in LLM_MODES and row.get("status") == "skipped-missing-api-key"
    ]
    lines.append(gate_line(
        not skipped_llm,
        "LLM-bearing cells scheduled",
        "no LLM-bearing cell was skipped for missing API key"
        if not skipped_llm else ", ".join(str(row.get("run_id")) for row in skipped_llm[:12]),
    ))
    lines.append(gate_line(
        llm_auth_errors == 0,
        "LLM auth errors",
        f"{llm_auth_errors} auth_error decision(s) across {len(llm_rows)} completed LLM-bearing runs",
    ))

    by_target_mode: dict[tuple[str, str], list[float]] = defaultdict(list)
    for row in done:
        eps = numeric(row, "execs_per_sec")
        if eps is not None:
            by_target_mode[(str(row.get("target", "")), str(row.get("mode", "")))].append(eps)

    full_rows = [row for row in done if row.get("mode") == "full-agent"]
    if profile == "infrastructure":
        lines.append(gate_line(
            True,
            "paper-evidence gates",
            "skipped for infrastructure acceptance profile",
        ))
    else:
        full_decisions = sum(int(row.get("agent_decisions") or 0) for row in full_rows)
        full_plateaus = sum(int(row.get("plateau_events") or 0) for row in full_rows)
        full_validation_decisions = sum(int(row.get("validation_decisions") or 0) for row in full_rows)
        lines.append(gate_line(
            bool(full_rows) and full_decisions >= min_decisions,
            "full-agent decisions",
            f"{full_decisions} decision(s), threshold {min_decisions}",
        ))
        lines.append(gate_line(
            bool(full_rows) and full_plateaus >= min_plateau,
            "full-agent plateau evidence",
            f"{full_plateaus} plateau event(s), threshold {min_plateau}",
        ))
        lines.append(gate_line(
            bool(full_rows) and full_validation_decisions >= 1,
            "full-agent validation evidence",
            f"{full_validation_decisions} validation winner decision event(s)",
        ))

        full_schema_rates = [
            parse_float(str(row.get("schema_valid_rate", "")))
            for row in full_rows if row.get("schema_valid_rate") != ""
        ]
        full_schema_rates = [value for value in full_schema_rates if value is not None]
        schema_value = min(full_schema_rates) if full_schema_rates else None
        lines.append(gate_line(
            schema_value is not None and schema_value >= min_schema,
            "full-agent schema-valid rate",
            "missing full-agent decision evidence"
            if schema_value is None else f"minimum run rate {schema_value:.3f}, threshold {min_schema:.3f}",
        ))

        ratios = []
        for target in sorted({str(row.get("target", "")) for row in done}):
            baseline = by_target_mode.get((target, "baseline-afl"), [])
            full = by_target_mode.get((target, "full-agent"), [])
            if baseline and full and median(baseline) > 0:
                ratios.append((target, median(full) / median(baseline)))
        bad_ratios = [(target, ratio) for target, ratio in ratios if ratio < min_throughput]
        ratio_detail = "not applicable until baseline-afl and full-agent complete for same target"
        if ratios:
            ratio_detail = ", ".join(f"{target}={ratio:.3f}" for target, ratio in ratios)
        lines.append(gate_line(
            bool(ratios) and not bad_ratios,
            "full-agent throughput ratio",
            f"{ratio_detail}; threshold {min_throughput:.3f}",
        ))

    required_metadata_paths = [
        ("run_order_index",),
        ("blocked_order_index",),
        ("cpu",),
        ("budget_sec",),
        ("git", "head"),
        ("git", "dirty"),
        ("host", "cpu_model"),
        ("host", "mem_total_kib"),
        ("host", "fuzz_slots"),
        ("tools", "fuzzpilot_sha256"),
        ("tools", "afl_fuzz_version"),
        ("target_artifacts", "target_binary_sha256"),
        ("target_artifacts", "static_analysis_enabled"),
        ("target_artifacts", "static_backend"),
        ("target_artifacts", "static_context_path"),
        ("target_artifacts", "static_context_sha256"),
        ("target_artifacts", "model_provider"),
        ("target_artifacts", "model_name"),
        ("target_artifacts", "model_endpoint_host"),
        ("target_artifacts", "api_key_env_state"),
    ]
    metadata_missing = []
    model_fingerprints = set()
    for row in rows_for_gates:
        meta = row.get("_metadata") if isinstance(row.get("_metadata"), dict) else {}
        artifacts = meta.get("target_artifacts") if isinstance(meta.get("target_artifacts"), dict) else {}
        if artifacts:
            model_fingerprints.add((
                artifacts.get("model_provider", ""),
                artifacts.get("model_name", ""),
                artifacts.get("model_endpoint_host", ""),
                artifacts.get("api_key_env", ""),
            ))
        missing = []
        for path in required_metadata_paths:
            current = row if len(path) == 1 and path[0] in row else meta
            for part in path:
                if not isinstance(current, dict) or part not in current:
                    current = None
                    break
                current = current[part]
            if current in ("", None, []):
                missing.append(".".join(path))
        if missing:
            metadata_missing.append(f"{row.get('run_id')}:{','.join(missing[:4])}")
    lines.append(gate_line(
        not metadata_missing,
        "runner metadata",
        "run order, CPU, git, toolchain, target artifact, and model metadata recorded"
        if not metadata_missing else ", ".join(metadata_missing[:12]),
    ))

    run_order_errors = []
    run_order_values = []
    blocked_order_values = []
    for row in rows_for_gates:
        run_id = str(row.get("run_id", ""))
        try:
            run_order_values.append(int(str(row.get("run_order_index", ""))))
        except ValueError:
            run_order_errors.append(f"{run_id}:run_order_index")
        try:
            blocked_order_values.append(int(str(row.get("blocked_order_index", ""))))
        except ValueError:
            run_order_errors.append(f"{run_id}:blocked_order_index")
    if run_order_values and len(run_order_values) != len(set(run_order_values)):
        run_order_errors.append("duplicate run_order_index")
    expected_count = len(expected_ids)
    if expected_count and sorted(run_order_values) != list(range(1, expected_count + 1)):
        run_order_errors.append(
            f"run_order_index range={min(run_order_values or [0])}-"
            f"{max(run_order_values or [0])}, expected=1-{expected_count}"
        )
    mode_count = max((len(stage_modes(manifest, stage_name)) for stage_name in stage_names), default=0)
    if mode_count and any(value < 1 or value > mode_count for value in blocked_order_values):
        run_order_errors.append(f"blocked_order_index outside 1-{mode_count}")
    lines.append(gate_line(
        not run_order_errors and bool(run_order_values),
        "runner ordering metadata",
        "unique contiguous run_order_index and in-block mode order recorded"
        if not run_order_errors and run_order_values else ", ".join(run_order_errors[:12]),
    ))
    lines.append(gate_line(
        len(model_fingerprints) == 1,
        "model configuration consistency",
        f"single model fingerprint={next(iter(model_fingerprints))}"
        if len(model_fingerprints) == 1 else
        f"model fingerprints={sorted(model_fingerprints)}",
    ))

    secret_hits = scan_forbidden(root, forbidden)
    lines.append(gate_line(
        not secret_hits,
        "forbidden secret patterns",
        "no configured secret patterns found"
        if not secret_hits else ", ".join(secret_hits[:12]),
    ))

    lines.extend(["", "## Per-Mode Summary", ""])
    lines.append(
        "| target | mode | statuses | exec/s median [IQR] | paths median [IQR] | "
        "schema-valid median [IQR] | decisions | validations | validated promotions | "
        "direct promotions | validated promotion rate |"
    )
    lines.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    groups: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows_for_gates:
        groups[(str(row.get("target", "")), str(row.get("mode", "")))].append(row)
    for (target, mode), group in sorted(groups.items()):
        complete_group = completed(group)
        status_counts_group: dict[str, int] = defaultdict(int)
        for row in group:
            status_counts_group[str(row.get("status", ""))] += 1
        execs = [numeric(row, "execs_per_sec") for row in complete_group]
        paths = [numeric(row, "paths_total") for row in complete_group]
        schema = [numeric(row, "schema_valid_rate") for row in complete_group]
        execs = [value for value in execs if value is not None]
        paths = [value for value in paths if value is not None]
        schema = [value for value in schema if value is not None]
        decisions = sum(int(row.get("agent_decisions") or 0) for row in group)
        validations = sum(int(row.get("validation_decisions") or 0) for row in group)
        validated_promotions = sum(int(row.get("promotions") or 0) for row in group)
        direct_promotions = sum(int(row.get("ai_direct_promotions") or 0) for row in group)
        promotion_rate = (
            f"{validated_promotions / validations:.3f}" if validations else "n/a"
        )
        lines.append(
            f"| {target} | {mode} | {dict(sorted(status_counts_group.items()))} | "
            f"{median_iqr_text(execs, 2)} | {median_iqr_text(paths, 0)} | "
            f"{median_iqr_text(schema, 3)} | {decisions} | {validations} | "
            f"{validated_promotions} | {direct_promotions} | {promotion_rate} |"
        )

    lines.extend(["", "## LLM Accounting", ""])
    lines.append(
        "| target | mode | decisions | input tokens | output tokens | "
        "latency ms median [IQR] | fallback rate median [IQR] |"
    )
    lines.append("|---|---|---:|---:|---:|---:|---:|")
    for (target, mode), group in sorted(groups.items()):
        decision_count = sum(int(row.get("agent_decisions") or 0) for row in group)
        input_tokens = sum(int(row.get("llm_input_tokens") or 0) for row in group)
        output_tokens = sum(int(row.get("llm_output_tokens") or 0) for row in group)
        latencies = [numeric(row, "llm_latency_ms_median") for row in group]
        fallbacks = [numeric(row, "fallback_rate") for row in group]
        latencies = [value for value in latencies if value is not None]
        fallbacks = [value for value in fallbacks if value is not None]
        if decision_count == 0 and input_tokens == 0 and output_tokens == 0:
            continue
        lines.append(
            f"| {target} | {mode} | {decision_count} | {input_tokens} | {output_tokens} | "
            f"{median_iqr_text(latencies, 0)} | {median_iqr_text(fallbacks, 3)} |"
        )

    lines.extend(["", "## Promotion Impact Windows", ""])
    lines.append(
        "| target | mode | promotion-bearing runs | paths +10m median [IQR] | "
        "edges +10m median [IQR] | paths +30m median [IQR] | edges +30m median [IQR] |"
    )
    lines.append("|---|---|---:|---:|---:|---:|---:|")
    for (target, mode), group in sorted(groups.items()):
        promotion_group = [
            row for row in group
            if int(row.get("promotions") or 0) > 0 or int(row.get("ai_direct_promotions") or 0) > 0
        ]
        if not promotion_group:
            continue
        paths_10m = [numeric(row, "post_promotion_paths_10m") for row in promotion_group]
        edges_10m = [numeric(row, "post_promotion_edges_10m") for row in promotion_group]
        paths_30m = [numeric(row, "post_promotion_paths_30m") for row in promotion_group]
        edges_30m = [numeric(row, "post_promotion_edges_30m") for row in promotion_group]
        paths_10m = [value for value in paths_10m if value is not None]
        edges_10m = [value for value in edges_10m if value is not None]
        paths_30m = [value for value in paths_30m if value is not None]
        edges_30m = [value for value in edges_30m if value is not None]
        lines.append(
            f"| {target} | {mode} | {len(promotion_group)} | "
            f"{median_iqr_text(paths_10m, 0)} | {median_iqr_text(edges_10m, 0)} | "
            f"{median_iqr_text(paths_30m, 0)} | {median_iqr_text(edges_30m, 0)} |"
        )

    lines.extend(["", "## Throughput Ratios", ""])
    lines.append("| target | mode | median exec/s | baseline median exec/s | ratio |")
    lines.append("|---|---|---:|---:|---:|")
    targets = sorted({str(row.get("target", "")) for row in done})
    modes = sorted({str(row.get("mode", "")) for row in done})
    for target in targets:
        baseline_values = by_target_mode.get((target, "baseline-afl"), [])
        if not baseline_values:
            continue
        baseline_median = median(baseline_values)
        if baseline_median <= 0:
            continue
        for mode in modes:
            values = by_target_mode.get((target, mode), [])
            if not values:
                continue
            mode_median = median(values)
            lines.append(
                f"| {target} | {mode} | {mode_median:.2f} | "
                f"{baseline_median:.2f} | {mode_median / baseline_median:.3f} |"
            )

    lines.extend(["", "## Architecture Claim Effect Sizes", ""])
    lines.append(
        "| claim tested | metric | comparison | paired cells | median ratio "
        "[bootstrap 95% CI] | paired win rate |"
    )
    lines.append("|---|---|---|---:|---:|---:|")
    claim_rows = [
        (
            "end-to-end architecture",
            "paths_total",
            "full-agent / baseline-afl",
            "full-agent",
            "baseline-afl",
        ),
        (
            "semantic context",
            "paths_total",
            "full-agent / no-semantic-context",
            "full-agent",
            "no-semantic-context",
        ),
        (
            "static grounding",
            "paths_total",
            "full-agent / no-static-analysis",
            "full-agent",
            "no-static-analysis",
        ),
        (
            "micro-campaign validation",
            "paths_total",
            "full-agent / ai-direct",
            "full-agent",
            "ai-direct",
        ),
        (
            "multi-agent vs coordinator",
            "paths_total",
            "full-agent / single-agent-coordinator",
            "full-agent",
            "single-agent-coordinator",
        ),
        (
            "multi-agent vs dictionary specialist",
            "paths_total",
            "full-agent / single-agent-dictionary",
            "full-agent",
            "single-agent-dictionary",
        ),
        (
            "recipe mutator interface",
            "paths_total",
            "full-agent / no-mutator",
            "full-agent",
            "no-mutator",
        ),
        (
            "throughput overhead",
            "execs_per_sec",
            "full-agent / baseline-afl",
            "full-agent",
            "baseline-afl",
        ),
    ]
    for claim, metric, comparison, numerator, denominator in claim_rows:
        ratios_for_claim = paired_ratios(done, numerator, denominator, metric)
        label = f"{','.join(stage_names)}:{claim}:{metric}:{comparison}"
        lines.append(
            f"| {claim} | {metric} | {comparison} | {len(ratios_for_claim)} | "
            f"{ratio_ci_text(ratios_for_claim, label)} | {win_rate_text(ratios_for_claim)} |"
        )
    lines.append("")
    lines.append(
        "Ratios above 1.0 favor the numerator mode. The bootstrap interval is "
        "deterministic and computed over paired target/repeat cells, so resumed "
        "runs remain auditable."
    )
    lines.append("")
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize FuzzPilot architecture pilot runs.")
    parser.add_argument(
        "root",
        nargs="?",
        default="results/fuzzpilot_architecture_pilot/runs",
        help="Directory containing per-run architecture pilot outputs.",
    )
    parser.add_argument(
        "--manifest",
        default="experiments/manifests/paper_architecture_pilot.yaml",
        help="Manifest with acceptance thresholds.",
    )
    parser.add_argument(
        "--csv",
        default="-",
        help="CSV output path. Use '-' for stdout.",
    )
    parser.add_argument(
        "--report",
        default="",
        help="Optional Markdown acceptance report path. Use '-' for stdout.",
    )
    parser.add_argument(
        "--stage",
        default="",
        help="Manifest stage to check for expected matrix completeness, e.g. A, B, or C.",
    )
    parser.add_argument(
        "--noise-summary",
        default="",
        help="Runtime noise summary JSON emitted by architecture_noise_monitor.py.",
    )
    parser.add_argument(
        "--stage-plan",
        default="",
        help="Stage plan JSON emitted by run_architecture_stage.sh.",
    )
    parser.add_argument(
        "--fail-on-gate-fail",
        action="store_true",
        help="Exit non-zero when the generated acceptance report contains a failed gate.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root)
    manifest_path = Path(args.manifest)
    manifest = load_manifest(manifest_path)
    manifest_sha256 = (
        hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        if manifest_path.exists() else ""
    )
    rows = collect_rows(root)
    if args.csv == "-":
        write_csv(rows, sys.stdout)
    else:
        csv_path = Path(args.csv)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_path.open("w", encoding="utf-8", newline="") as out:
            writer = csv.DictWriter(out, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows({field: row.get(field, "") for field in FIELDS} for row in rows)

    report = ""
    if args.report or args.fail_on_gate_fail:
        noise_summary = load_noise_summary(Path(args.noise_summary)) if args.noise_summary else {}
        stage_plan = load_noise_summary(Path(args.stage_plan)) if args.stage_plan else {}
        report = generate_report(
            rows,
            manifest,
            root,
            args.stage,
            noise_summary,
            stage_plan,
            manifest_sha256,
        )
    if args.report:
        if args.report == "-":
            print(report)
        else:
            report_path = Path(args.report)
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(report + "\n", encoding="utf-8")
    if args.fail_on_gate_fail and "- [FAIL]" in report:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
