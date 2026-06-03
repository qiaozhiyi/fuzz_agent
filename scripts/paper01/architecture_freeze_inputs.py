#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import stat
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlparse


def load_yaml(path: Path) -> dict:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(f"PyYAML is required: {exc}")
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as inp:
        for chunk in iter(lambda: inp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, root: Path) -> dict:
    resolved = root / path if not path.is_absolute() else path
    if not resolved.exists():
        return {
            "path": str(path),
            "exists": False,
            "size": 0,
            "sha256": "",
            "executable": False,
        }
    mode = resolved.stat().st_mode
    return {
        "path": str(path),
        "exists": True,
        "size": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
        "executable": bool(mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)),
    }


def directory_files(path: Path, root: Path) -> list[dict]:
    resolved = root / path if not path.is_absolute() else path
    if not resolved.is_dir():
        return []
    records = []
    for item in sorted(resolved.rglob("*")):
        if item.is_file():
            rel = item.relative_to(root) if item.is_relative_to(root) else item
            records.append(file_record(rel, root))
    return records


def run_text(cmd: list[str]) -> str:
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return result.stdout.strip()


def target_fingerprint(root: Path, target: dict) -> dict:
    config_path = Path(str(target.get("config") or ""))
    config = load_yaml(root / config_path)
    target_cfg = config.get("target") or {}
    static_cfg = config.get("static_analysis") or {}
    model_cfg = config.get("model_api") or {}
    endpoint = str(model_cfg.get("endpoint") or "")

    binary = Path(str(target_cfg.get("binary") or ""))
    seed_dir = Path(str(target_cfg.get("input_dir") or ""))
    dict_path = Path(str(target_cfg.get("dict") or ""))
    static_context = Path(str(static_cfg.get("context_path") or ""))

    return {
        "id": target.get("id", ""),
        "name": target.get("name", ""),
        "config": file_record(config_path, root),
        "binary": file_record(binary, root),
        "seed_dir": str(seed_dir),
        "seeds": directory_files(seed_dir, root),
        "dict": file_record(dict_path, root) if str(dict_path) else {},
        "static_context": file_record(static_context, root) if str(static_context) else {},
        "target_runtime": {
            "args": list(target_cfg.get("args") or []),
            "timeout_ms": int(target_cfg.get("timeout_ms") or 0),
            "memory_mb": int(target_cfg.get("memory_mb") or 0),
        },
        "model_api": {
            "provider": model_cfg.get("provider", ""),
            "endpoint_host": urlparse(endpoint).netloc,
            "endpoint_path": urlparse(endpoint).path,
            "model": model_cfg.get("model", ""),
            "api_key_env": model_cfg.get("api_key_env", ""),
            "disable_thinking": bool(model_cfg.get("disable_thinking", False)),
        },
    }


def build_fingerprint(root: Path, manifest_path: Path, stage_name: str) -> dict:
    manifest = load_yaml(root / manifest_path)
    stage = (manifest.get("stages") or {}).get(stage_name) or {}
    defaults = manifest.get("defaults") or {}
    return {
        "stage": stage_name,
        "manifest": file_record(manifest_path, root),
        "paper": manifest.get("paper", ""),
        "version": manifest.get("version", ""),
        "defaults": {
            "llm_parallel": defaults.get("llm_parallel", ""),
            "non_llm_parallel": defaults.get("non_llm_parallel", ""),
        },
        "stage_config": {
            "acceptance_profile": stage.get("acceptance_profile", "paper"),
            "repeats": int(stage.get("repeats") or 0),
            "budget_sec": int(stage.get("budget_sec") or 0),
            "modes": list(stage.get("modes") or manifest.get("modes") or []),
        },
        "acceptance": manifest.get("acceptance") or {},
        "targets": [target_fingerprint(root, target) for target in manifest.get("targets") or []],
        "git": {
            "head": run_text(["git", "rev-parse", "HEAD"]),
            "dirty": bool(run_text(["git", "status", "--porcelain"])),
        },
    }


def default_freeze_path(root: Path, stage_name: str) -> Path:
    return root / "results/fuzzpilot_architecture_pilot/frozen_inputs" / f"stage_{stage_name.lower()}_inputs.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Freeze and verify architecture-pilot experiment inputs."
    )
    parser.add_argument("stage", nargs="?", default="B", help="Stage name: S, A, B, or C.")
    parser.add_argument(
        "--manifest",
        default="experiments/manifests/paper_architecture_pilot.yaml",
        help="Architecture pilot manifest.",
    )
    parser.add_argument("--freeze", default="", help="Freeze JSON path.")
    parser.add_argument("--write", action="store_true", help="Write the current fingerprint.")
    parser.add_argument("--check", action="store_true", help="Compare current inputs with the frozen fingerprint.")
    parser.add_argument("--overwrite", action="store_true", help="Allow --write to replace an existing freeze file.")
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stage = args.stage.upper()
    root = Path.cwd()
    manifest_path = Path(args.manifest)
    freeze_path = Path(args.freeze) if args.freeze else default_freeze_path(root, stage)
    current = {
        "created_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "fingerprint": build_fingerprint(root, manifest_path, stage),
    }

    if args.write:
        if freeze_path.exists() and not args.overwrite:
            print(f"freeze file already exists: {freeze_path}", file=sys.stderr)
            print("use --overwrite only for a deliberate new exploratory stage", file=sys.stderr)
            return 2
        freeze_path.parent.mkdir(parents=True, exist_ok=True)
        freeze_path.write_text(json.dumps(current, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(json.dumps(current, indent=2, sort_keys=True) if args.json else f"frozen inputs written: {freeze_path}")
        return 0

    if args.check:
        if not freeze_path.is_file():
            print(f"frozen inputs missing: {freeze_path}", file=sys.stderr)
            return 2
        try:
            frozen = json.loads(freeze_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"failed to read frozen inputs: {exc}", file=sys.stderr)
            return 2
        if frozen.get("fingerprint") != current["fingerprint"]:
            payload = {
                "ok": False,
                "freeze": str(freeze_path),
                "message": "current architecture-pilot inputs differ from the frozen Stage inputs",
                "frozen": frozen.get("fingerprint"),
                "current": current["fingerprint"],
            }
            print(json.dumps(payload, indent=2, sort_keys=True) if args.json else payload["message"])
            return 2
        payload = {"ok": True, "freeze": str(freeze_path), "stage": stage}
        print(json.dumps(payload, indent=2, sort_keys=True) if args.json else f"frozen inputs match: {freeze_path}")
        return 0

    print(json.dumps(current, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
