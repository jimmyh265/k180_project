#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

BIN="${BIN:-build/grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer12_nvvidconvbuf8_appsinkmaxbuf2_triinterusleep}"
START_EXPOSURES="${START_EXPOSURES:-${EXPOSURES:-1000 2000 4000}}"
RUNS="${RUNS:-3}"
ROUNDS="${ROUNDS:-6}"
MAX_SEC="${MAX_SEC:-420}"
WARMUP_SEC="${WARMUP_SEC:-10}"
INTER_DELAYS="${INTER_DELAYS:-0}"
TSD="${TSD:-0}"
PATTERN="${PATTERN:-n=30}"
REQUIRED_LOG_PATTERN="${REQUIRED_LOG_PATTERN:-[FPS][nvvidconv_in_}"
MIN_EXPOSURE="${MIN_EXPOSURE:-50}"
MAX_EXPOSURE="${MAX_EXPOSURE:-8000}"
RESOLUTION_US="${RESOLUTION_US:-250}"

if [ ! -x "$BIN" ]; then
  echo "not executable: $BIN" >&2
  exit 1
fi

mkdir -p log

root_ts=$(date +%Y%m%d_%H%M%S)
root_dir="log/exposure_converge_${root_ts}"
mkdir -p "$root_dir"
summary="${root_dir}/summary.csv"

echo "round,exposure,total,valid,fail,status,round_dir" > "$summary"
cat > "${root_dir}/config.txt" <<EOF
BIN=${BIN}
START_EXPOSURES=${START_EXPOSURES}
RUNS=${RUNS}
ROUNDS=${ROUNDS}
MAX_SEC=${MAX_SEC}
WARMUP_SEC=${WARMUP_SEC}
INTER_DELAYS=${INTER_DELAYS}
TSD=${TSD}
PATTERN=${PATTERN}
REQUIRED_LOG_PATTERN=${REQUIRED_LOG_PATTERN}
MIN_EXPOSURE=${MIN_EXPOSURE}
MAX_EXPOSURE=${MAX_EXPOSURE}
RESOLUTION_US=${RESOLUTION_US}
EOF

sort_exposures() {
  printf "%s\n" "$@" | awk 'NF && !seen[$0]++' | sort -n | tr '\n' ' '
}

make_triple() {
  sort_exposures "$1" "$2" "$3"
}

count_words() {
  local s="$1"
  set -- $s
  echo "$#"
}

max_int() {
  if [ "$1" -gt "$2" ]; then echo "$1"; else echo "$2"; fi
}

min_int() {
  if [ "$1" -lt "$2" ]; then echo "$1"; else echo "$2"; fi
}

status_for() {
  local total="$1"
  local valid="$2"
  local fail="$3"
  if [ "$total" -lt "$RUNS" ]; then
    echo "incomplete"
  elif [ "$valid" -lt "$total" ]; then
    echo "invalid"
  elif [ "$fail" -eq 0 ]; then
    echo "stable"
  elif [ "$fail" -eq 1 ]; then
    echo "borderline"
  else
    echo "unstable"
  fi
}

current="$(sort_exposures $START_EXPOSURES)"

for round in $(seq 1 "$ROUNDS"); do
  if [ "$(count_words "$current")" -ne 3 ]; then
    echo "round ${round}: need exactly 3 unique exposure values, got: ${current}" | tee -a "${root_dir}/decision.log"
    break
  fi

  round_dir="${root_dir}/round_${round}_$(echo "$current" | tr ' ' '_' | sed 's/_$//')"
  mkdir -p "$round_dir"

  echo "=== round ${round} exposures=${current} runs=${RUNS} max_sec=${MAX_SEC} warmup=${WARMUP_SEC}s itd=${INTER_DELAYS} tsd=${TSD} ===" | tee -a "${root_dir}/decision.log"

  before_file="$(mktemp)"
  after_file="$(mktemp)"
  find log -maxdepth 1 -type f -name 'exp*run_*.log' -printf '%f\n' | sort > "$before_file"

  BIN="$BIN" \
  EXPOSURES="$current" \
  RUNS="$RUNS" \
  MAX_SEC="$MAX_SEC" \
  WARMUP_SEC="$WARMUP_SEC" \
  INTER_DELAYS="$INTER_DELAYS" \
  PATTERN="$PATTERN" \
    ./run_trigger_signal_delay_sweep.sh "$TSD" 2>&1 | tee "${round_dir}/console.log"
  rc=${PIPESTATUS[0]}

  find log -maxdepth 1 -type f -name 'exp*run_*.log' -printf '%f\n' | sort > "$after_file"
  comm -13 "$before_file" "$after_file" | while IFS= read -r name; do
    [ -n "$name" ] || continue
    mv "log/${name}" "${round_dir}/${name}"
  done
  rm -f "$before_file" "$after_file"

  if [ "$rc" -ne 0 ]; then
    echo "round ${round}: worker failed rc=${rc}" | tee -a "${root_dir}/decision.log"
    exit "$rc"
  fi

  read -r -a exps <<< "$current"
  stable_count=0
  invalid_count=0
  lowest_stable=0
  highest_unstable=0
  non_monotonic=0
  seen_stable=0

  for exp in "${exps[@]}"; do
    total=$(grep -F "=== exp${exp} " "${round_dir}/console.log" | grep -F " done:" | wc -l | tr -d ' ')
    valid=$(find "$round_dir" -maxdepth 1 -type f -name "exp${exp}_tsd*_itd*_run_*.log" -exec grep -l -F "$REQUIRED_LOG_PATTERN" {} \; | wc -l | tr -d ' ')
    fail=$(grep -F "=== exp${exp} " "${round_dir}/console.log" | grep -F " done:" | grep -F "reason=found_${PATTERN}" | wc -l | tr -d ' ')
    status=$(status_for "$total" "$valid" "$fail")
    echo "${round},${exp},${total},${valid},${fail},${status},${round_dir}" >> "$summary"
    echo "round ${round}: exp=${exp} total=${total} valid=${valid} fail=${fail} status=${status}" | tee -a "${root_dir}/decision.log"

    if [ "$status" = "stable" ]; then
      stable_count=$((stable_count + 1))
      seen_stable=1
      if [ "$lowest_stable" -eq 0 ] || [ "$exp" -lt "$lowest_stable" ]; then
        lowest_stable="$exp"
      fi
    elif [ "$status" = "invalid" ]; then
      invalid_count=$((invalid_count + 1))
      if [ "$exp" -gt "$highest_unstable" ]; then
        highest_unstable="$exp"
      fi
    else
      if [ "$seen_stable" -eq 1 ]; then
        non_monotonic=1
      fi
      if [ "$exp" -gt "$highest_unstable" ]; then
        highest_unstable="$exp"
      fi
    fi
  done

  if [ "$non_monotonic" -eq 1 ]; then
    echo "round ${round}: non-monotonic result; continue conservatively using lowest stable=${lowest_stable}" | tee -a "${root_dir}/decision.log"
  fi

  if [ "$invalid_count" -gt 0 ]; then
    echo "round ${round}: invalid logs detected (${invalid_count}); missing required pattern '${REQUIRED_LOG_PATTERN}'. Stop and fix probe/logging before continuing." | tee -a "${root_dir}/decision.log"
    break
  elif [ "$stable_count" -eq 0 ]; then
    high="${exps[2]}"
    if [ "$high" -ge "$MAX_EXPOSURE" ]; then
      echo "round ${round}: no stable exposure up to ${MAX_EXPOSURE}; stop" | tee -a "${root_dir}/decision.log"
      break
    fi
    next_high=$(min_int "$MAX_EXPOSURE" "$((high * 2))")
    next_mid=$(((high + next_high) / 2))
    next="$(make_triple "$high" "$next_mid" "$next_high")"
    echo "round ${round}: no stable value; search higher -> ${next}" | tee -a "${root_dir}/decision.log"
  elif [ "$stable_count" -eq 3 ]; then
    low="${exps[0]}"
    if [ "$low" -le "$MIN_EXPOSURE" ]; then
      echo "round ${round}: all stable and reached min exposure; recommended=${low}" | tee -a "${root_dir}/decision.log"
      break
    fi
    next_low=$(max_int "$MIN_EXPOSURE" "$((low / 2))")
    next_mid=$(((next_low + low) / 2))
    next="$(make_triple "$next_low" "$next_mid" "$low")"
    echo "round ${round}: all stable; search lower -> ${next}" | tee -a "${root_dir}/decision.log"
  else
    hi="$lowest_stable"
    lo=0
    for exp in "${exps[@]}"; do
      if [ "$exp" -lt "$hi" ]; then
        total=$(grep -F "=== exp${exp} " "${round_dir}/console.log" | grep -F " done:" | wc -l | tr -d ' ')
        valid=$(find "$round_dir" -maxdepth 1 -type f -name "exp${exp}_tsd*_itd*_run_*.log" -exec grep -l -F "$REQUIRED_LOG_PATTERN" {} \; | wc -l | tr -d ' ')
        fail=$(grep -F "=== exp${exp} " "${round_dir}/console.log" | grep -F " done:" | grep -F "reason=found_${PATTERN}" | wc -l | tr -d ' ')
        status=$(status_for "$total" "$valid" "$fail")
        if [ "$status" != "stable" ] && [ "$exp" -gt "$lo" ]; then
          lo="$exp"
        fi
      fi
    done

    if [ "$lo" -eq 0 ]; then
      next_low=$(max_int "$MIN_EXPOSURE" "$((hi / 2))")
      next_mid=$(((next_low + hi) / 2))
      next="$(make_triple "$next_low" "$next_mid" "$hi")"
      echo "round ${round}: stable exists but no lower failing point; search lower -> ${next}" | tee -a "${root_dir}/decision.log"
    else
      width=$((hi - lo))
      if [ "$width" -le "$RESOLUTION_US" ]; then
        echo "round ${round}: bracket ${lo}..${hi} within ${RESOLUTION_US}us; recommended=${hi}" | tee -a "${root_dir}/decision.log"
        break
      fi
      mid=$(((lo + hi) / 2))
      next="$(make_triple "$lo" "$mid" "$hi")"
      echo "round ${round}: bracket ${lo}..${hi}; refine -> ${next}" | tee -a "${root_dir}/decision.log"
    fi
  fi

  if [ "$next" = "$current" ] || [ "$(count_words "$next")" -ne 3 ]; then
    echo "round ${round}: cannot make progress from '${current}' to '${next}'; stop" | tee -a "${root_dir}/decision.log"
    break
  fi
  current="$next"
done

echo "summary: ${summary}"
echo "decision log: ${root_dir}/decision.log"
