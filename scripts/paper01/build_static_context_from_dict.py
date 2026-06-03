#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path


def read_dict_tokens(path: Path, limit: int) -> list[str]:
    tokens = []
    seen = set()
    if not path.exists():
        return tokens
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if len(line) >= 2 and line[0] == '"' and line[-1] == '"':
            try:
                token = bytes(line[1:-1], "utf-8").decode("unicode_escape")
            except UnicodeDecodeError:
                token = line[1:-1]
        else:
            token = line
        if not token or "\n" in token or "\r" in token:
            continue
        if token not in seen:
            seen.add(token)
            tokens.append(token)
        if len(tokens) >= limit:
            break
    return tokens


def sha256(path: Path) -> str:
    if not path.exists():
        return ""
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build a compact static context JSON from a precomputed dictionary."
    )
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--dict", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--backend", default="ghidra-precomputed")
    parser.add_argument("--limit", type=int, default=256)
    args = parser.parse_args()

    dict_path = Path(args.dict)
    binary_path = Path(args.binary)
    tokens = read_dict_tokens(dict_path, args.limit)
    context = {
        "backend": args.backend,
        "program": binary_path.name,
        "target_id": args.target_id,
        "source_dict": str(dict_path),
        "source_dict_sha256": sha256(dict_path),
        "target_binary": str(binary_path),
        "target_binary_sha256": sha256(binary_path),
        "functions": [],
        "magic_tokens": tokens,
        "cmp_constants": [],
        "structs": {},
        "decompiled_logic": {},
        "branch_constraints": [],
        "provenance": {
            "generated_by": "scripts/paper01/build_static_context_from_dict.py",
            "token_limit": args.limit,
            "token_count": len(tokens),
        },
    }

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(context, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out} token_count={len(tokens)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
