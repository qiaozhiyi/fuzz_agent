#!/usr/bin/env bash
# scripts/paper01/auto_swap_watchdog.sh
#
# 周期监控 DeepSeek 余额 + agent_decisions.jsonl 错误率，
# 触发条件满足时自动调用 swap_to_nvidia.sh 切换到 NVIDIA NIM。
#
# 触发条件（任一即可）:
#   - DeepSeek CNY 余额 < ${BAL_THRESHOLD:-0.5}
#   - 任何 full-agent run 的 agent_decisions.jsonl 含 http_error 条目
#
# 用法（在远程 tmux 单独 window 里跑）:
#   bash /root/fuzz_agent/scripts/paper01/auto_swap_watchdog.sh
#
# 或者后台跑:
#   tmux new-window -t paper01: -n watchdog \
#     "bash /root/fuzz_agent/scripts/paper01/auto_swap_watchdog.sh"

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RESULTS_ROOT="${REPO_ROOT}/results/paper01_ai_recipe_mutation"
SWAP_SCRIPT="${REPO_ROOT}/scripts/paper01/swap_to_nvidia.sh"

# 阈值（可通过环境变量 override）
BAL_THRESHOLD="${BAL_THRESHOLD:-0.5}"
POLL_SEC="${POLL_SEC:-300}"    # 默认 5 分钟一次
DEEPSEEK_KEY="${DEEPSEEK_KEY:-sk-c2d7d08c539049fe8192353ce0f6529b}"

LOG_DIR="${RESULTS_ROOT}/runs/_logs"
mkdir -p "${LOG_DIR}"
WD_LOG="${LOG_DIR}/auto_swap_watchdog.log"

ts() { date -u +"%Y-%m-%dT%H:%M:%SZ"; }
log() { local m="[$(ts)] $*"; echo "$m"; echo "$m" >> "${WD_LOG}"; }

log "watchdog start: bal<${BAL_THRESHOLD} or any http_error → swap"
log "polling every ${POLL_SEC}s; log: ${WD_LOG}"

while true; do
  # --- check 1: DeepSeek 余额 ---
  bal_raw=$(curl -sS --max-time 10 -H "Authorization: Bearer ${DEEPSEEK_KEY}" \
    https://api.deepseek.com/user/balance 2>/dev/null)
  cny_bal=$(echo "$bal_raw" | python3 -c "
import json, sys
try:
    d = json.load(sys.stdin)
    for b in d.get('balance_infos', []):
        if b.get('currency') == 'CNY':
            print(b.get('total_balance', '0'))
            break
    else:
        print('NaN')
except Exception:
    print('NaN')
" 2>/dev/null)

  # --- check 2: agent_decisions.jsonl 中是否有 http_error ---
  http_err_total=0
  for d in "${RESULTS_ROOT}/runs"/p1_e*_full-agent_*/; do
    log_file="${d}/agent_decisions.jsonl"
    [ -f "${log_file}" ] || continue
    n=$(grep -c '"error_kind":"http_error"' "${log_file}" 2>/dev/null || echo 0)
    http_err_total=$((http_err_total + n))
  done

  log "balance=¥${cny_bal} http_errors_seen=${http_err_total}"

  # --- 触发判定 ---
  trigger=""
  if [ "${cny_bal}" != "NaN" ]; then
    is_low=$(python3 -c "
try:
    print('1' if float('${cny_bal}') < float('${BAL_THRESHOLD}') else '0')
except: print('0')
")
    if [ "${is_low}" = "1" ]; then
      trigger="balance ¥${cny_bal} < ¥${BAL_THRESHOLD}"
    fi
  fi

  if [ "${http_err_total}" -gt 0 ]; then
    trigger="${trigger:+${trigger}; }seen ${http_err_total} http_error entries"
  fi

  if [ -n "${trigger}" ]; then
    log "TRIGGER: ${trigger}"
    log "calling swap script: ${SWAP_SCRIPT}"
    bash "${SWAP_SCRIPT}" 2>&1 | tee -a "${WD_LOG}"
    log "swap done; watchdog exits"
    exit 0
  fi

  sleep "${POLL_SEC}"
done
