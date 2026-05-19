#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

EXPOSURES="${EXPOSURES:-${EXPOSURE:-100}}"
RUNS="${RUNS:-1}"
MAX_SEC="${MAX_SEC:-420}"
WARMUP_SEC="${WARMUP_SEC:-10}"
INTER_DELAYS="${INTER_DELAYS:-0}"
DELAYS=(0)

CASES=(
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer12_nvvidconvbuf12"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer12_nvvidconvbuf8"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer16_nvvidconvbuf12"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer16_nvvidconvbuf16"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer16_nvvidconvbuf32"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer16_nvvidconvbuf8"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer20_nvvidconvbuf12"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer20_nvvidconvbuf16"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer8_nvvidconvbuf12"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer8_nvvidconvbuf8"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer12_nvvidconvbuf8_appsinkmaxbuf2_triorder0to3"
  # "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer12_nvvidconvbuf8_appsinkmaxbuf2_triorder3to0"
  "grand_yeah_console_short_usleep1000_manualshutter_manuakTSD_srccapbuffer12_nvvidconvbuf8_appsinkmaxbuf2_triinterusleep"
)

mkdir -p log

for bin_name in "${CASES[@]}"; do
  bin_path="build/${bin_name}"
  if [ ! -x "$bin_path" ]; then
    echo "=== skip ${bin_name}: not executable: ${bin_path} ==="
    continue
  fi

  case_ts=$(date +%Y%m%d_%H%M%S)
  out_dir="log/sweep_${bin_name}_${case_ts}"
  mkdir -p "$out_dir"

  echo "=== case ${bin_name} start: out_dir=${out_dir} exposures=${EXPOSURES} runs=${RUNS} max_sec=${MAX_SEC} warmup=${WARMUP_SEC}s delays=${DELAYS[*]} inter_delays=${INTER_DELAYS} ==="

  before_file="$(mktemp)"
  after_file="$(mktemp)"
  find log -maxdepth 1 -type f \( -name 'exp*run_*.log' -o -name 'tsd*run_*.log' \) -printf '%f\n' | sort > "$before_file"

  BIN="$bin_path" EXPOSURES="$EXPOSURES" RUNS="$RUNS" MAX_SEC="$MAX_SEC" WARMUP_SEC="$WARMUP_SEC" INTER_DELAYS="$INTER_DELAYS" ./run_trigger_signal_delay_sweep.sh "${DELAYS[@]}" \
    2>&1 | tee "${out_dir}/console.log"
  rc=${PIPESTATUS[0]}

  find log -maxdepth 1 -type f \( -name 'exp*run_*.log' -o -name 'tsd*run_*.log' \) -printf '%f\n' | sort > "$after_file"
  comm -13 "$before_file" "$after_file" | while IFS= read -r name; do
    [ -n "$name" ] || continue
    mv "log/${name}" "${out_dir}/${name}"
  done
  rm -f "$before_file" "$after_file"

  echo "=== case ${bin_name} done: exit=${rc} out_dir=${out_dir} ==="
  if [ "$rc" -ne 0 ]; then
    exit "$rc"
  fi

  sleep 30
done
