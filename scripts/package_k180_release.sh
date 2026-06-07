#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

RELEASE_NAME="${RELEASE_NAME:-k180_release_$(date +%Y%m%d_%H%M%S)}"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp}"
RELEASE_ROOT="${OUTPUT_DIR%/}/${RELEASE_NAME}"

DEFAULT_RTSP_PROFILE="${DEFAULT_RTSP_PROFILE:-fps60_short}"
DEFAULT_RTSP_PROFILE="${DEFAULT_RTSP_PROFILE//-/_}"
DEFAULT_DEC_USER="${DEFAULT_DEC_USER:-fourd}"

YOLO_ENGINE="${YOLO_ENGINE:-/home/jimnt/k180_release_assets/yolov8n_fp16.engine}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-change-this-password-before-release}"
USERS_JSON="${USERS_JSON:-}"

BUILD="${BUILD:-1}"
CREATE_ZIP="${CREATE_ZIP:-1}"

rtsp_bins=(
  grand_yeah_fps60_short
  grand_yeah_fps60_long
  grand_yeah_fps30_short
  grand_yeah_fps30_long
)

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

require_file() {
  local path="$1"
  [[ -f "$path" ]] || die "missing required file: $path"
}

if [[ "$BUILD" != "0" ]]; then
  make -C "$ROOT_DIR/rtsp_server" all-profiles
  make -C "$ROOT_DIR/restful_api"
  make -C "$ROOT_DIR/gen_fw"
  make -C "$ROOT_DIR/system_helpers"
fi

for bin in "${rtsp_bins[@]}"; do
  require_file "$ROOT_DIR/rtsp_server/build/$bin"
done
require_file "$ROOT_DIR/rtsp_server/build/grand_yeah_${DEFAULT_RTSP_PROFILE}"
require_file "$ROOT_DIR/restful_api/build/restful_api_server"
require_file "$ROOT_DIR/gen_fw/build/gen_fw_dec_${DEFAULT_DEC_USER}"
require_file "$ROOT_DIR/system_helpers/build/modify_sysip_netplan"
require_file "$ROOT_DIR/system_helpers/build/reboot_system"
require_file "$ROOT_DIR/system_helpers/build/restart_program_camera"
require_file "$YOLO_ENGINE"

dec_bins=()
for path in "$ROOT_DIR"/gen_fw/build/gen_fw_dec_*; do
  [[ -f "$path" ]] || continue
  dec_bins+=("$path")
done
[[ "${#dec_bins[@]}" -gt 0 ]] || die "no gen_fw_dec_* binaries found in gen_fw/build"

mkdir -p "$OUTPUT_DIR"
rm -rf "$RELEASE_ROOT"
mkdir -p \
  "$RELEASE_ROOT/usr/local/bin" \
  "$RELEASE_ROOT/usr/local/libexec/k180" \
  "$RELEASE_ROOT/usr/local/share/k180" \
  "$RELEASE_ROOT/etc/k180" \
  "$RELEASE_ROOT/var/lib/k180" \
  "$RELEASE_ROOT/var/www/html" \
  "$RELEASE_ROOT/var/www/private" \
  "$RELEASE_ROOT/etc/systemd/system" \
  "$RELEASE_ROOT/etc/sudoers.d"

for bin in "${rtsp_bins[@]}"; do
  install -m 0755 "$ROOT_DIR/rtsp_server/build/$bin" "$RELEASE_ROOT/usr/local/bin/$bin"
done
install -m 0755 \
  "$ROOT_DIR/rtsp_server/build/grand_yeah_${DEFAULT_RTSP_PROFILE}" \
  "$RELEASE_ROOT/usr/local/bin/grand_yeah"
install -m 0755 \
  "$ROOT_DIR/restful_api/build/restful_api_server" \
  "$RELEASE_ROOT/usr/local/bin/restful_api_server"

for path in "${dec_bins[@]}"; do
  install -m 0750 "$path" "$RELEASE_ROOT/usr/local/libexec/k180/$(basename "$path")"
done
install -m 0750 \
  "$ROOT_DIR/gen_fw/build/gen_fw_dec_${DEFAULT_DEC_USER}" \
  "$RELEASE_ROOT/usr/local/libexec/k180/gen_fw_dec"
install -m 0750 \
  "$ROOT_DIR/system_helpers/build/modify_sysip_netplan" \
  "$RELEASE_ROOT/usr/local/libexec/k180/modify_sysip_netplan"
install -m 0750 \
  "$ROOT_DIR/system_helpers/build/reboot_system" \
  "$RELEASE_ROOT/usr/local/libexec/k180/reboot_system"
install -m 0750 \
  "$ROOT_DIR/system_helpers/build/restart_program_camera" \
  "$RELEASE_ROOT/usr/local/libexec/k180/restart_program_camera"

install -m 0644 "$YOLO_ENGINE" "$RELEASE_ROOT/usr/local/share/k180/yolov8n_fp16.engine"
install -m 0644 "$ROOT_DIR/docs/K180_DEPLOYMENT.md" "$RELEASE_ROOT/K180_DEPLOYMENT.md"

install -m 0664 \
  "$ROOT_DIR/rtsp_server/cfg/user_def_setting.json" \
  "$RELEASE_ROOT/etc/k180/user_def_setting.json"
install -m 0640 \
  "$ROOT_DIR/rtsp_server/cfg/rescue_sys_ip.json" \
  "$RELEASE_ROOT/etc/k180/rescue_sys_ip.json"
cp -a "$ROOT_DIR/rtsp_server/cfg/meta1234_rotate_0" "$RELEASE_ROOT/etc/k180/"
cp -a "$ROOT_DIR/rtsp_server/cfg/meta1234_rotate_180" "$RELEASE_ROOT/etc/k180/"

install -m 0644 \
  "$ROOT_DIR/rtsp_server/cfg/firmware_ver_info.json" \
  "$RELEASE_ROOT/var/lib/k180/firmware_ver_info.json"
install -m 0644 \
  "$ROOT_DIR/rtsp_server/cfg/api_ver_info.json" \
  "$RELEASE_ROOT/var/lib/k180/api_ver_info.json"

rsync -a --delete \
  --exclude='tmp_fw/' \
  --exclude='tmp_rec_zip/' \
  "$ROOT_DIR/www_html/" \
  "$RELEASE_ROOT/var/www/html/"

if [[ -n "$USERS_JSON" ]]; then
  require_file "$USERS_JSON"
  install -m 0660 "$USERS_JSON" "$RELEASE_ROOT/var/www/private/users.json"
else
  command -v php >/dev/null 2>&1 || die "php is required to generate users.json; install php-cli or pass USERS_JSON=/path/to/users.json"
  if [[ "$ADMIN_PASSWORD" == "change-this-password-before-release" ]]; then
    printf 'WARNING: using default ADMIN_PASSWORD. Set ADMIN_PASSWORD before production release.\n' >&2
  fi
  admin_hash="$(php -r 'echo password_hash($argv[1], PASSWORD_DEFAULT);' "$ADMIN_PASSWORD")"
  cat > "$RELEASE_ROOT/var/www/private/users.json" <<EOF
[
  {
    "username": "admin",
    "password_hash": "${admin_hash}",
    "role": "admin"
  }
]
EOF
  chmod 0660 "$RELEASE_ROOT/var/www/private/users.json"
fi

cat > "$RELEASE_ROOT/etc/systemd/system/grand_yeah.service" <<'EOF'
[Unit]
Description=Fourd K180 Program
After=multi-user.target nvidia-persistenced.service
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
ExecStart=/usr/local/bin/grand_yeah
User=fourd
Restart=on-failure
RestartSec=5

StandardOutput=journal
StandardError=journal
SyslogIdentifier=grand_yeah

[Install]
WantedBy=multi-user.target
EOF

cat > "$RELEASE_ROOT/etc/systemd/system/restful_api.service" <<'EOF'
[Unit]
Description=Fourd K180 RESTful API Server
After=multi-user.target network-online.target
Wants=network-online.target
StartLimitIntervalSec=60
StartLimitBurst=5

[Service]
ExecStart=/usr/local/bin/restful_api_server
User=fourd
Restart=on-failure
RestartSec=5

StandardOutput=journal
StandardError=journal
SyslogIdentifier=restful_api

[Install]
WantedBy=multi-user.target
EOF

cat > "$RELEASE_ROOT/etc/sudoers.d/k180" <<'EOF'
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/gen_fw_dec *
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/modify_sysip_netplan
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/reboot_system
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/restart_program_camera

fourd ALL=(root) NOPASSWD: /usr/local/libexec/k180/modify_sysip_netplan
fourd ALL=(root) NOPASSWD: /usr/local/libexec/k180/reboot_system
fourd ALL=(root) NOPASSWD: /usr/local/libexec/k180/restart_program_camera
EOF
chmod 0440 "$RELEASE_ROOT/etc/sudoers.d/k180"

git_commit="$(git -C "$ROOT_DIR" rev-parse --short=12 HEAD 2>/dev/null || printf unknown)"
git_describe="$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || printf unknown)"
if [[ -z "$(git -C "$ROOT_DIR" status --porcelain 2>/dev/null)" ]]; then
  git_dirty=0
else
  git_dirty=1
fi

{
  printf 'release_name=%s\n' "$RELEASE_NAME"
  printf 'created_at=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'git_commit=%s\n' "$git_commit"
  printf 'git_describe=%s\n' "$git_describe"
  printf 'git_dirty=%s\n' "$git_dirty"
  printf 'default_rtsp_profile=%s\n' "$DEFAULT_RTSP_PROFILE"
  printf 'default_dec_user=%s\n' "$DEFAULT_DEC_USER"
  printf 'included_rtsp_binaries=\n'
  printf '  %s\n' "${rtsp_bins[@]}"
  printf 'included_gen_fw_dec_binaries=\n'
  for path in "${dec_bins[@]}"; do
    printf '  %s\n' "$(basename "$path")"
  done
  printf 'yolo_engine_sha256=%s\n' "$(sha256sum "$YOLO_ENGINE" | awk '{print $1}')"
} > "$RELEASE_ROOT/MANIFEST.txt"

tar_path="${OUTPUT_DIR%/}/${RELEASE_NAME}.tar.gz"
zip_path="${OUTPUT_DIR%/}/${RELEASE_NAME}.zip"

rm -f "$tar_path" "$zip_path"
tar -C "$OUTPUT_DIR" -czf "$tar_path" "$RELEASE_NAME"

if [[ "$CREATE_ZIP" != "0" ]]; then
  if command -v zip >/dev/null 2>&1; then
    (cd "$OUTPUT_DIR" && zip -qr "${RELEASE_NAME}.zip" "$RELEASE_NAME")
  else
    printf 'WARNING: zip not found; skipped zip artifact.\n' >&2
  fi
fi

sha256sum "$tar_path"
if [[ -f "$zip_path" ]]; then
  sha256sum "$zip_path"
fi

printf 'Release root: %s\n' "$RELEASE_ROOT"
