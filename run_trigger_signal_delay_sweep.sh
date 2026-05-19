#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

BIN="${BIN:-./build/grand_yeah_console_short}"
EXPOSURES="${EXPOSURES:-${EXPOSURE:-100}}"
RUNS="${RUNS:-1}"
MAX_SEC="${MAX_SEC:-420}"
WARMUP_SEC="${WARMUP_SEC:-10}"
CHECK_SEC="${CHECK_SEC:-10}"
KILL_AFTER_SEC="${KILL_AFTER_SEC:-10}"
PATTERN="${PATTERN:-n=30}"
INTER_DELAYS="${INTER_DELAYS:-0}"
STREAM_READY_PATTERN="${STREAM_READY_PATTERN:-INFO: stream ready}"

if [ "$#" -gt 0 ]; then
  DELAYS=("$@")
else
  DELAYS=(0)
fi

mkdir -p log

read -r -a EXPOSURE_LIST <<< "$EXPOSURES"
read -r -a INTER_DELAY_LIST <<< "$INTER_DELAYS"

run_one() {
  local exposure="$1"
  local delay_us="$2"
  local inter_delay_us="$3"
  local run_idx="$4"
  local ts
  ts=$(date +%Y%m%d_%H%M%S)
  local log_file="log/exp${exposure}_tsd${delay_us}_itd${inter_delay_us}_run_${run_idx}_${ts}.log"

  echo "=== exp${exposure} tsd${delay_us} itd${inter_delay_us} run ${run_idx} start: ${log_file} warmup=${WARMUP_SEC}s after_stream_ready ==="

  stdbuf -oL -eL "$BIN" \
    --exposure="$exposure" \
    --trigger-signal-delay-us="$delay_us" \
    --inter-trigger-delay-us="$inter_delay_us" \
    > "$log_file" 2>&1 &
  local pid=$!

  local start
  start=$(date +%s)
  local reason="timeout"
  local stream_ready_seen=0
  local stream_ready_line=0
  local stream_ready_time=0
  local warmup_done=0
  local warmup_line=0

  while kill -0 "$pid" 2>/dev/null; do
    local now elapsed
    now=$(date +%s)
    elapsed=$((now - start))

    if [ "$stream_ready_seen" -eq 0 ]; then
      local ready_match
      ready_match=$(grep -n -m 1 -F "$STREAM_READY_PATTERN" "$log_file" 2>/dev/null || true)
      if [ -n "$ready_match" ]; then
        stream_ready_seen=1
        stream_ready_line="${ready_match%%:*}"
        stream_ready_time="$now"
        echo "=== exp${exposure} tsd${delay_us} itd${inter_delay_us} run ${run_idx} stream_ready: line=${stream_ready_line}, warmup=${WARMUP_SEC}s ==="
      fi
    fi

    if [ "$stream_ready_seen" -eq 1 ] && [ "$((now - stream_ready_time))" -ge "$WARMUP_SEC" ]; then
      if [ "$warmup_done" -eq 0 ]; then
        warmup_line=$(wc -l < "$log_file" 2>/dev/null || echo 0)
        if [ "$warmup_line" -lt "$stream_ready_line" ]; then
          warmup_line="$stream_ready_line"
        fi
        warmup_done=1
        echo "=== exp${exposure} tsd${delay_us} itd${inter_delay_us} run ${run_idx} watch_start: line=${warmup_line} ==="
      fi
      if tail -n +"$((warmup_line + 1))" "$log_file" 2>/dev/null | tail -n 50 | grep -q "$PATTERN"; then
        reason="found_${PATTERN}"
        break
      fi
    fi

    if [ "$elapsed" -ge "$MAX_SEC" ]; then
      if [ "$stream_ready_seen" -eq 0 ]; then
        reason="timeout_before_stream_ready"
      else
        reason="timeout_5min"
      fi
      break
    fi

    sleep "$CHECK_SEC"
  done

  if kill -0 "$pid" 2>/dev/null; then
    echo "=== exp${exposure} tsd${delay_us} itd${inter_delay_us} run ${run_idx} stop: ${reason}, send INT ==="
    kill -INT "$pid" 2>/dev/null

    for _ in $(seq 1 "$KILL_AFTER_SEC"); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 1
    done

    if kill -0 "$pid" 2>/dev/null; then
      echo "=== exp${exposure} tsd${delay_us} itd${inter_delay_us} run ${run_idx} still alive, send KILL ==="
      kill -KILL "$pid" 2>/dev/null
    fi
  fi

  wait "$pid" 2>/dev/null
  local rc=$?

  echo "=== exp${exposure} tsd${delay_us} itd${inter_delay_us} run ${run_idx} done: reason=${reason} exit=${rc} log=${log_file} ==="
  sleep 15
}

for exposure in "${EXPOSURE_LIST[@]}"; do
  for delay_us in "${DELAYS[@]}"; do
    for inter_delay_us in "${INTER_DELAY_LIST[@]}"; do
      for i in $(seq 1 "$RUNS"); do
        run_one "$exposure" "$delay_us" "$inter_delay_us" "$i"
      done
    done
  done
done
