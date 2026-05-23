#!/usr/bin/env bash
# scripts/paper01/swap_to_nvidia.sh
#
# 紧急切换：DeepSeek 余额耗尽 → NVIDIA NIM 接力
# 在远程跑 (ssh fuzz-server 后执行)
#
# 触发条件:
#   - DeepSeek balance < ¥0.5  OR
#   - agent_decisions.jsonl 出现 http_error
#
# 行为:
#   1. cp .nvidia.bak → config.yaml
#   2. 把 secrets 换成 NVIDIA NIM key
#   3. kill 正在跑的 full-agent run (污染中)
#   4. 清掉 http_error > 25% 的 completed run
#   5. tmux session 不动，新 fork 的 run_batch 自动拿新 config

set -e
cd /root/fuzz_agent

NVIDIA_KEY="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"

echo "[1/5] swap config to NVIDIA NIM"
cp experiments/targets/cjson/config.yaml.nvidia.bak experiments/targets/cjson/config.yaml
grep -E 'endpoint:|model:' experiments/targets/cjson/config.yaml

echo "[2/5] swap API key"
cat > /root/.fuzzpilot_secrets <<EOF
export FUZZPILOT_MODEL_API_KEY=${NVIDIA_KEY}
EOF
chmod 600 /root/.fuzzpilot_secrets

echo "[3/5] kill in-flight full-agent runs (data being polluted)"
pkill -f "fuzzpilot run.*--ablation full-agent" 2>/dev/null || true
sleep 3

echo "[4/5] purge polluted runs (http_error > 25%)"
purged=0
for d in /root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/p1_e*_full-agent_*; do
  log="$d/agent_decisions.jsonl"
  [ -f "$log" ] || continue
  total=$(wc -l < "$log")
  err=$(grep -c '"error_kind":"http_error"' "$log" 2>/dev/null || echo 0)
  if [ "$total" -gt 0 ] && [ "$err" -gt "$((total / 4))" ]; then
    echo "  purging $(basename $d) (err=$err/$total)"
    rm -rf "$d/work" "$d/status" "$d/stdout.log" "$d/stderr.log" "$d/agent_decisions.jsonl"
    purged=$((purged + 1))
  fi
done
echo "  purged $purged runs (--resume will retry them)"

echo "[5/5] check tmux"
if tmux ls 2>/dev/null | grep -q paper01; then
  echo "  tmux paper01 alive; new run_batch forks will pick up new config + key"
else
  echo "  tmux paper01 dead; restarting"
  tmux new-session -d -s paper01 -c /root/fuzz_agent \
    "bash scripts/paper01/run_all_host.sh --parallel 4 2>&1 | tee /root/fuzz_agent/results/paper01_ai_recipe_mutation/runs/_logs/MASTER_resume.log"
fi
echo ""
echo "DONE: backend = NVIDIA NIM (llama-4-maverick)"
