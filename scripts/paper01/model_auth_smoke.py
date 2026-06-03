#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_FREE_GLM_CANDIDATES = [
    "glm-4.7-flash",
    "glm-4-flash-250414",
]


def load_config(path: Path) -> dict:
    try:
        import yaml
    except ImportError as exc:
        raise SystemExit(f"PyYAML is required: {exc}")
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def classify_status(status: int, body: str) -> str:
    if status in {401, 403}:
        return "auth_error"
    if status in {429, 503}:
        return "rate_limited"
    if 400 <= status < 600:
        return "http_error"
    if status == 200:
        return "ok"
    return "unknown"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate OpenAI-compatible model credentials without logging secrets."
    )
    parser.add_argument(
        "--config",
        default="experiments/targets/libxml2/config_glm.yaml",
        help="Target config containing model_api settings.",
    )
    parser.add_argument(
        "--model",
        action="append",
        default=[],
        help="Override the config model. May be repeated to test multiple models.",
    )
    parser.add_argument(
        "--free-glm-candidates",
        action="store_true",
        help="Test the current official free GLM text candidates used by this project.",
    )
    parser.add_argument("--timeout-sec", type=int, default=20)
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    parser.add_argument(
        "--show-response",
        action="store_true",
        help="Include a short sanitized response excerpt for functional smoke checks.",
    )
    return parser.parse_args()


def candidate_models(args: argparse.Namespace, configured_model: str) -> list[str]:
    models = []
    if args.free_glm_candidates:
        models.extend(DEFAULT_FREE_GLM_CANDIDATES)
    models.extend(args.model)
    if not models and configured_model:
        models.append(configured_model)

    seen = set()
    ordered = []
    for model in models:
        model = str(model).strip()
        if model and model not in seen:
            seen.add(model)
            ordered.append(model)
    return ordered


def short_response_excerpt(body: str) -> str:
    try:
        parsed = json.loads(body)
        choices = parsed.get("choices") or []
        if choices:
            message = choices[0].get("message") or {}
            content = message.get("content") or message.get("reasoning_content") or ""
            if isinstance(content, str) and content:
                return content[:160].replace("\n", "\\n")
    except Exception:
        pass
    return body[:160].replace("\n", "\\n")


def smoke_one(args: argparse.Namespace, model_cfg: dict, model: str) -> dict:
    endpoint = str(model_cfg.get("endpoint") or "")
    key_env = str(model_cfg.get("api_key_env") or "")
    disable_thinking = bool(model_cfg.get("disable_thinking", False))
    key = os.environ.get(key_env, "")

    started = time.time()
    result = {
        "config": args.config,
        "provider": model_cfg.get("provider", ""),
        "endpoint_host": "",
        "model": model,
        "api_key_env": key_env,
        "api_key_env_state": "set" if key else "unset",
        "http_status": 0,
        "error_kind": "",
        "latency_ms": 0,
        "response_excerpt": "",
    }

    try:
        from urllib.parse import urlparse
        result["endpoint_host"] = urlparse(endpoint).netloc
    except Exception:
        result["endpoint_host"] = ""

    if not endpoint or not model or not key_env:
        result["error_kind"] = "bad_config"
        return result
    if not key:
        result["error_kind"] = "auth_error"
        return result

    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": "Return a tiny JSON object."},
            {"role": "user", "content": "{\"task\":\"auth_smoke\"}"},
        ],
        "response_format": {"type": "json_object"},
        "max_tokens": 16,
        "temperature": 0.0,
        "top_p": 1.0,
    }
    if disable_thinking:
        payload["thinking"] = {"type": "disabled"}

    request = urllib.request.Request(
        endpoint,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + key,
        },
        method="POST",
    )

    body = ""
    try:
        with urllib.request.urlopen(request, timeout=args.timeout_sec) as response:
            status = int(response.status)
            body = response.read(4096).decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        status = int(exc.code)
        body = exc.read(4096).decode("utf-8", errors="replace")
    except Exception as exc:
        status = 0
        body = str(exc)
        result["error_kind"] = "transport_error"
    if not result["error_kind"]:
        result["error_kind"] = classify_status(status, body)
    result["http_status"] = status
    result["latency_ms"] = int((time.time() - started) * 1000)
    if args.show_response and body:
        result["response_excerpt"] = short_response_excerpt(body)
    return result


def print_text_result(result: dict) -> None:
    print(
        "model auth smoke: "
        f"provider={result['provider']} model={result['model']} host={result['endpoint_host']} "
        f"key_env={result['api_key_env']} key_state={result['api_key_env_state']} "
        f"http_status={result['http_status']} error_kind={result['error_kind']} "
        f"latency_ms={result['latency_ms']}"
    )
    if result.get("response_excerpt"):
        print(f"model response excerpt: {result['response_excerpt']}")


def main() -> int:
    args = parse_args()
    cfg = load_config(Path(args.config))
    model_cfg = cfg.get("model_api") or {}
    configured_model = str(model_cfg.get("model") or "")
    models = candidate_models(args, configured_model)
    if not models:
        result = {
            "config": args.config,
            "provider": model_cfg.get("provider", ""),
            "endpoint_host": "",
            "model": "",
            "api_key_env": str(model_cfg.get("api_key_env") or ""),
            "api_key_env_state": "unset",
            "http_status": 0,
            "error_kind": "bad_config",
            "latency_ms": 0,
            "response_excerpt": "",
        }
        print(json.dumps(result, indent=2, sort_keys=True) if args.json else "model auth smoke: bad_config")
        return 2

    results = [smoke_one(args, model_cfg, model) for model in models]

    if args.json:
        payload = results[0] if len(results) == 1 else {"results": results}
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        for result in results:
            print_text_result(result)
    return 0 if any(result["error_kind"] == "ok" for result in results) else 2


if __name__ == "__main__":
    raise SystemExit(main())
