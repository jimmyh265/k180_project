#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

BIN="${BIN:-./build/grand_yeah_console_short}"
RUNS="${RUNS:-3}"
MAX_SEC="${MAX_SEC:-300}"
WARMUP_SEC="${WARMUP_SEC:-60}"
CHECK_SEC="${CHECK_SEC:-10}"
KILL_AFTER_SEC="${KILL_AFTER_SEC:-10}"
COOLDOWN_SEC="${COOLDOWN_SEC:-30}"
PATTERN="${PATTERN:-n=30}"

EXPOSURES=("$@")
if [ ${#EXPOSURES[@]} -eq 0 ]; then
  EXPOSURES=(100 250 500 1000 2000 4000)
fi

mkdir -p log

for exposure in "${EXPOSURES[@]}"; do
  for run in $(seq 1 "$RUNS"); do
    ts=$(date +%Y%m%d_%H%M%S)
    log_file="log/exposure${exposure}_run_${run}_${ts}.log"

    echo "=== exposure=${exposure} run ${run} start: ${log_file} ==="
    echo "[TEST_CASE] exposure=${exposure} run=${run} max_sec=${MAX_SEC} warmup_sec=${WARMUP_SEC} cooldown_sec=${COOLDOWN_SEC} watch=n_eq_30 start=$(date '+%F %T')" > "$log_file"

    stdbuf -oL -eL "$BIN" --exposure="$exposure" >> "$log_file" 2>&1 &
    pid=$!

    start=$(date +%s)
    reason="timeout"

    while kill -0 "$pid" 2>/dev/null; do
      now=$(date +%s)
      elapsed=$((now - start))

      if [ "$elapsed" -ge "$WARMUP_SEC" ]; then
        if tail -n 50 "$log_file" 2>/dev/null | grep -v '^\[TEST_CASE\]' | grep -q "$PATTERN"; then
          reason="found_${PATTERN}"
          break
        fi
      fi

      if [ $((now - start)) -ge "$MAX_SEC" ]; then
        reason="timeout_5min"
        break
      fi

      sleep "$CHECK_SEC"
    done

    if kill -0 "$pid" 2>/dev/null; then
      echo "[TEST_CASE] stop reason=${reason} time=$(date '+%F %T')" >> "$log_file"
      kill -INT "$pid" 2>/dev/null

      for _ in $(seq 1 "$KILL_AFTER_SEC"); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
      done

      if kill -0 "$pid" 2>/dev/null; then
        echo "[TEST_CASE] still alive, send KILL time=$(date '+%F %T')" >> "$log_file"
        kill -KILL "$pid" 2>/dev/null
      fi
    fi

    wait "$pid" 2>/dev/null
    rc=$?

    echo "[TEST_CASE] done exposure=${exposure} run=${run} reason=${reason} exit=${rc} end=$(date '+%F %T')" >> "$log_file"
    echo "=== exposure=${exposure} run ${run} done: reason=${reason} exit=${rc} ==="

    if [ "$COOLDOWN_SEC" -gt 0 ]; then
      echo "=== exposure=${exposure} run ${run} cooldown: ${COOLDOWN_SEC}s ==="
      sleep "$COOLDOWN_SEC"
    fi
  done
done
