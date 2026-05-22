#!/usr/bin/env bash
# scripts/paper01/filter_stdout_logs.sh
#
# 把已完成（status=completed）的 run 目录下 stdout.log 里 AFL 的
# "Fuzzing test case #N ..." 进度噪声过滤掉，保留 banner / WARNING / ERROR /
# 启动配置 / fuzzpilot 最终 JSON 摘要 / AFL 最终统计行。
#
# 安全性：
#   - 只处理 status=completed 的 run（in-flight 的 status 是 "running" 不会被动）
#   - 校验 WARNING / banner / fuzzpilot summary 行数过滤前后必须相等
#   - 校验通过才原子 mv 替换；否则保留原文件不变
#
# Usage:
#   scripts/paper01/filter_stdout_logs.sh                       # 默认 paper01 runs
#   scripts/paper01/filter_stdout_logs.sh --root <runs-dir>     # 指定目录
#   scripts/paper01/filter_stdout_logs.sh --dry-run             # 只统计，不动
#
# Idempotent：filter 过的文件不含 "Fuzzing test case" 行，二次运行会
# 看到 0 行可删，跳过。

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROOT="${REPO_ROOT}/results/paper01_ai_recipe_mutation/runs"
DRY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root)    ROOT="$2"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

[[ -d "${ROOT}" ]] || { echo "ERROR: dir not found: ${ROOT}" >&2; exit 2; }

total_before=0
total_after=0
filtered_count=0
skipped_count=0

# grep -c prints the count (incl. 0) and exits 1 on zero matches; we want to
# treat that exit-1 as "0 matches" without ALSO emitting an extra echo line.
count() { local n; n="$(grep -cF "$1" "$2" 2>/dev/null)"; printf '%s\n' "${n:-0}"; }
count_re() { local n; n="$(grep -c "$1" "$2" 2>/dev/null)"; printf '%s\n' "${n:-0}"; }

for d in "${ROOT}"/*/; do
  [[ -d "$d" ]] || continue
  status_file="$d/status"
  log_file="$d/stdout.log"
  run_name="$(basename "$d")"

  if [[ ! -f "$status_file" ]] || [[ "$(cat "$status_file")" != "completed" ]]; then
    skipped_count=$((skipped_count + 1))
    continue
  fi
  [[ -f "$log_file" ]] || { skipped_count=$((skipped_count + 1)); continue; }

  noise_lines="$(count "Fuzzing test case" "$log_file")"
  if [[ "$noise_lines" -eq 0 ]]; then
    # already filtered or natively clean
    skipped_count=$((skipped_count + 1))
    continue
  fi

  before_bytes=$(wc -c < "$log_file")
  total_before=$((total_before + before_bytes))

  if [[ ${DRY} -eq 1 ]]; then
    after_bytes_est=$(grep -vF "Fuzzing test case" "$log_file" | wc -c)
    saved=$((before_bytes - after_bytes_est))
    total_after=$((total_after + after_bytes_est))
    printf "  [DRY] %-50s %8s noise lines, would save %s\n" \
      "$run_name" "$noise_lines" "$(awk -v b=$saved 'BEGIN{
        if (b > 1048576) printf "%.1f MB", b/1048576;
        else if (b > 1024) printf "%.1f KB", b/1024;
        else printf "%d B", b
      }')"
    filtered_count=$((filtered_count + 1))
    continue
  fi

  # 三道校验线：noise 之外的关键内容数量必须前后相等
  warn_before=$(count_re "WARNING" "$log_file")
  banner_before=$(count "afl-fuzz++" "$log_file")
  summary_before=$(count_re '^{"run_id"' "$log_file")

  tmp="${log_file}.filter.$$"
  grep -vF "Fuzzing test case" "$log_file" > "$tmp"
  warn_after=$(count_re "WARNING" "$tmp")
  banner_after=$(count "afl-fuzz++" "$tmp")
  summary_after=$(count_re '^{"run_id"' "$tmp")

  if [[ "$warn_before" == "$warn_after" \
     && "$banner_before" == "$banner_after" \
     && "$summary_before" == "$summary_after" ]]; then
    mv "$tmp" "$log_file"
    after_bytes=$(wc -c < "$log_file")
    total_after=$((total_after + after_bytes))
    saved=$((before_bytes - after_bytes))
    printf "  %-50s %8s noise lines removed, saved %s\n" \
      "$run_name" "$noise_lines" "$(awk -v b=$saved 'BEGIN{
        if (b > 1048576) printf "%.1f MB", b/1048576;
        else if (b > 1024) printf "%.1f KB", b/1024;
        else printf "%d B", b
      }')"
    filtered_count=$((filtered_count + 1))
  else
    rm -f "$tmp"
    total_after=$((total_after + before_bytes))
    printf "  [SKIP-CHECKSUM-FAIL] %s — WARNING/banner/summary count drift; left unchanged\n" "$run_name" >&2
    skipped_count=$((skipped_count + 1))
  fi
done

echo
echo "Processed:  filtered=${filtered_count}  skipped=${skipped_count}"
saved_total=$((total_before - total_after))
if [[ $saved_total -gt 0 ]]; then
  saved_human=$(awk -v b=$saved_total 'BEGIN{
    if (b > 1073741824) printf "%.2f GB", b/1073741824;
    else if (b > 1048576) printf "%.1f MB", b/1048576;
    else if (b > 1024) printf "%.1f KB", b/1024;
    else printf "%d B", b
  }')
  if [[ ${DRY} -eq 1 ]]; then
    echo "Would save: ${saved_human}"
  else
    echo "Saved:      ${saved_human}"
  fi
else
  echo "Nothing to filter."
fi
