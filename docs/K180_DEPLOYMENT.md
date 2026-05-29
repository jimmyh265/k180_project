# K180 Deployment Guide

這份文件整理 K180 產品機的部署方式。目標是假設產品機不 git clone 完整 source code，只拿 release package 裡必要的執行檔、設定檔、web 檔案與 service 檔案做安裝。

## 建議策略

1. Source code 放在 GitHub private repository。
2. 每次出貨或發版時，從已驗證的 build machine 產生一包 release artifact。
3. 產品機只下載 release artifact，不下載完整 source。
4. `gen_fw_enc_*` 是內部/出貨端工具，不放到產品機。
5. `gen_fw_dec` 是產品機用來解 firmware package 的工具，放到 `/usr/local/libexec/k180/gen_fw_dec`。

注意：`grand_yeah` 不是只靠 bare Ubuntu 就一定能跑。它依賴 NVIDIA Jetson/CUDA/TensorRT/DeepStream/OpenCV/GStreamer 等 runtime。產品機的 Ubuntu image 必須已經包含和 build machine 相容的 runtime。

## Release Package 建議結構

建議 release package 展開後長這樣：

```text
k180_release/
  usr/local/bin/
    grand_yeah
    restful_api_server

  usr/local/libexec/k180/
    gen_fw_dec
    modify_sysip_netplan
    reboot_system
    restart_program_camera

  usr/local/share/k180/
    yolov8n_fp16.engine

  etc/k180/
    user_def_setting.json
    rescue_sys_ip.json
    meta1234_rotate_0/
    meta1234_rotate_180/

  var/lib/k180/
    firmware_ver_info.json
    api_ver_info.json

  var/www/html/
    index.php
    lang/
    php_src/

  var/www/private/
    users.json

  etc/systemd/system/
    grand_yeah.service
    restful_api.service

  etc/sudoers.d/
    k180
```

## 在 Build Machine 產生 Release Package

在 `/home/jimnt/projects` 執行：

```bash
cd /home/jimnt/projects

make -C rtsp_server fps60-short
make -C restful_api
make -C gen_fw
make -C system_helpers

RELEASE_NAME=k180_release_$(date +%Y%m%d_%H%M%S)
RELEASE_ROOT=/tmp/${RELEASE_NAME}

rm -rf "${RELEASE_ROOT}"
mkdir -p \
  "${RELEASE_ROOT}/usr/local/bin" \
  "${RELEASE_ROOT}/usr/local/libexec/k180" \
  "${RELEASE_ROOT}/usr/local/share/k180" \
  "${RELEASE_ROOT}/etc/k180" \
  "${RELEASE_ROOT}/var/lib/k180" \
  "${RELEASE_ROOT}/var/www/html" \
  "${RELEASE_ROOT}/var/www/private" \
  "${RELEASE_ROOT}/etc/systemd/system" \
  "${RELEASE_ROOT}/etc/sudoers.d"

install -m 0755 rtsp_server/build/grand_yeah_fps60_short "${RELEASE_ROOT}/usr/local/bin/grand_yeah"
install -m 0755 restful_api/build/restful_api_server "${RELEASE_ROOT}/usr/local/bin/restful_api_server"

install -m 0750 gen_fw/build/gen_fw_dec_fourd "${RELEASE_ROOT}/usr/local/libexec/k180/gen_fw_dec"
install -m 0750 system_helpers/build/modify_sysip_netplan "${RELEASE_ROOT}/usr/local/libexec/k180/modify_sysip_netplan"
install -m 0750 system_helpers/build/reboot_system "${RELEASE_ROOT}/usr/local/libexec/k180/reboot_system"
install -m 0750 system_helpers/build/restart_program_camera "${RELEASE_ROOT}/usr/local/libexec/k180/restart_program_camera"

install -m 0644 rtsp_server/yolov8n_fp16.engine "${RELEASE_ROOT}/usr/local/share/k180/yolov8n_fp16.engine"

install -m 0664 rtsp_server/cfg/user_def_setting.json "${RELEASE_ROOT}/etc/k180/user_def_setting.json"
install -m 0640 rtsp_server/cfg/rescue_sys_ip.json "${RELEASE_ROOT}/etc/k180/rescue_sys_ip.json"
cp -a rtsp_server/cfg/meta1234_rotate_0 "${RELEASE_ROOT}/etc/k180/"
cp -a rtsp_server/cfg/meta1234_rotate_180 "${RELEASE_ROOT}/etc/k180/"

install -m 0644 rtsp_server/cfg/firmware_ver_info.json "${RELEASE_ROOT}/var/lib/k180/firmware_ver_info.json"
install -m 0644 rtsp_server/cfg/api_ver_info.json "${RELEASE_ROOT}/var/lib/k180/api_ver_info.json"

rsync -a --delete --exclude='tmp_fw/' --exclude='tmp_rec_zip/' www_html/ "${RELEASE_ROOT}/var/www/html/"
```

建立 `users.json` 初始檔。請依產品需求改密碼後再打包：

```bash
ADMIN_PASSWORD='change-this-password-before-release'
ADMIN_HASH=$(php -r 'echo password_hash($argv[1], PASSWORD_DEFAULT);' "$ADMIN_PASSWORD")

cat > "${RELEASE_ROOT}/var/www/private/users.json" <<EOF
[
  {
    "username": "admin",
    "password_hash": "${ADMIN_HASH}",
    "role": "admin"
  }
]
EOF
chmod 0660 "${RELEASE_ROOT}/var/www/private/users.json"
```

建立 systemd service：

```bash
cat > "${RELEASE_ROOT}/etc/systemd/system/grand_yeah.service" <<'EOF'
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

cat > "${RELEASE_ROOT}/etc/systemd/system/restful_api.service" <<'EOF'
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
```

建立 sudoers 檔：

```bash
cat > "${RELEASE_ROOT}/etc/sudoers.d/k180" <<'EOF'
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/gen_fw_dec *
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/modify_sysip_netplan
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/reboot_system
www-data ALL=(root) NOPASSWD: /usr/local/libexec/k180/restart_program_camera

fourd ALL=(root) NOPASSWD: /usr/local/libexec/k180/modify_sysip_netplan
fourd ALL=(root) NOPASSWD: /usr/local/libexec/k180/reboot_system
fourd ALL=(root) NOPASSWD: /usr/local/libexec/k180/restart_program_camera
EOF
chmod 0440 "${RELEASE_ROOT}/etc/sudoers.d/k180"
```

打包。Linux 部署建議用 `tar.gz`，權限較清楚；若流程要求 zip，也可以同時產生 zip。

```bash
tar -C /tmp -czf "/tmp/${RELEASE_NAME}.tar.gz" "${RELEASE_NAME}"

cd /tmp
zip -r "${RELEASE_NAME}.zip" "${RELEASE_NAME}"

sha256sum "/tmp/${RELEASE_NAME}.tar.gz" "/tmp/${RELEASE_NAME}.zip"
```

## 第一次部署到產品機

以下假設 release package 已解到 `/tmp/k180_release`。

### 1. 安裝基本套件與檢查 runtime

```bash
sudo apt update
sudo apt install -y apache2 php php-cli php-zip rsync zip netplan.io network-manager libmicrohttpd12 libjson-c5 libssl3 libuuid1
```

`grand_yeah` 需要 NVIDIA/CUDA/TensorRT/DeepStream/OpenCV/GStreamer runtime。這些通常不是 bare Ubuntu 內建，應該由產品 OS image 預先準備。安裝後可用 `ldd` 檢查：

```bash
ldd /tmp/k180_release/usr/local/bin/grand_yeah | grep 'not found' || true
ldd /tmp/k180_release/usr/local/bin/restful_api_server | grep 'not found' || true
ldd /tmp/k180_release/usr/local/libexec/k180/gen_fw_dec | grep 'not found' || true
ldd /tmp/k180_release/usr/local/libexec/k180/modify_sysip_netplan | grep 'not found' || true
```

### 2. 建立 user/group

```bash
sudo groupadd -f k180

if ! id fourd >/dev/null 2>&1; then
  sudo useradd -m -s /bin/bash -G k180,video,render,i2c,gpio fourd
else
  sudo usermod -aG k180,video,render,i2c,gpio fourd
fi

sudo usermod -aG k180 www-data
```

### 3. 建立目錄

```bash
sudo install -d -o root -g k180 -m 0750 /etc/k180
sudo install -d -o root -g k180 -m 0750 /var/lib/k180
sudo install -d -o root -g k180 -m 2770 /var/lib/k180/tmp_fw /var/lib/k180/tmp_rec_zip
sudo install -d -o root -g root -m 0755 /usr/local/bin
sudo install -d -o root -g k180 -m 0750 /usr/local/libexec/k180
sudo install -d -o root -g root -m 0755 /usr/local/share/k180
sudo install -d -o root -g root -m 0755 /var/www/html
sudo install -d -o root -g k180 -m 0750 /var/www/private
sudo install -d -o root -g k180 -m 2770 /data
```

### 4. 安裝檔案

```bash
cd /tmp/k180_release

sudo install -m 0755 -o root -g root usr/local/bin/grand_yeah /usr/local/bin/grand_yeah
sudo install -m 0755 -o root -g root usr/local/bin/restful_api_server /usr/local/bin/restful_api_server

sudo install -m 0750 -o root -g k180 usr/local/libexec/k180/gen_fw_dec /usr/local/libexec/k180/gen_fw_dec
sudo install -m 0750 -o root -g k180 usr/local/libexec/k180/modify_sysip_netplan /usr/local/libexec/k180/modify_sysip_netplan
sudo install -m 0750 -o root -g k180 usr/local/libexec/k180/reboot_system /usr/local/libexec/k180/reboot_system
sudo install -m 0750 -o root -g k180 usr/local/libexec/k180/restart_program_camera /usr/local/libexec/k180/restart_program_camera

sudo install -m 0644 -o root -g root usr/local/share/k180/yolov8n_fp16.engine /usr/local/share/k180/yolov8n_fp16.engine

sudo install -m 0664 -o root -g k180 etc/k180/user_def_setting.json /etc/k180/user_def_setting.json
sudo install -m 0640 -o root -g k180 etc/k180/rescue_sys_ip.json /etc/k180/rescue_sys_ip.json
sudo rsync -a --delete etc/k180/meta1234_rotate_0 /etc/k180/
sudo rsync -a --delete etc/k180/meta1234_rotate_180 /etc/k180/
sudo chown -R root:k180 /etc/k180/meta1234_rotate_0 /etc/k180/meta1234_rotate_180
sudo find /etc/k180/meta1234_rotate_0 /etc/k180/meta1234_rotate_180 -type d -exec chmod 0750 {} +
sudo find /etc/k180/meta1234_rotate_0 /etc/k180/meta1234_rotate_180 -type f -exec chmod 0640 {} +

sudo install -m 0644 -o fourd -g k180 var/lib/k180/firmware_ver_info.json /var/lib/k180/firmware_ver_info.json
sudo install -m 0644 -o fourd -g k180 var/lib/k180/api_ver_info.json /var/lib/k180/api_ver_info.json
sudo install -m 0664 -o root -g k180 /dev/null /var/lib/k180/tmp_rec_zip/zip_batch.lock

sudo rsync -a --delete --chown=root:root --chmod=D755,F644 var/www/html/ /var/www/html/
sudo install -m 0660 -o root -g k180 var/www/private/users.json /var/www/private/users.json

sudo install -m 0644 -o root -g root etc/systemd/system/grand_yeah.service /etc/systemd/system/grand_yeah.service
sudo install -m 0644 -o root -g root etc/systemd/system/restful_api.service /etc/systemd/system/restful_api.service
sudo install -m 0440 -o root -g root etc/sudoers.d/k180 /etc/sudoers.d/k180
sudo visudo -cf /etc/sudoers.d/k180
```

### 5. Netplan

`modify_sysip_netplan` 會產生 `/etc/k180/01-netcfg.yaml`。`/etc/netplan/01-netcfg.yaml` 建議固定連到它。

```bash
sudo /usr/local/libexec/k180/modify_sysip_netplan

sudo rm -f /etc/netplan/01-netcfg.yaml
sudo ln -s /etc/k180/01-netcfg.yaml /etc/netplan/01-netcfg.yaml

sudo netplan generate --debug
sudo netplan try --timeout 60
```

### 6. 啟動服務

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now grand_yeah.service restful_api.service

systemctl is-enabled grand_yeah.service restful_api.service
systemctl is-active grand_yeah.service restful_api.service
```

### 7. 檢查權限

```bash
sudo stat -c '%A %U:%G %n' \
  /etc/k180 \
  /etc/k180/user_def_setting.json \
  /etc/k180/rescue_sys_ip.json \
  /etc/k180/01-netcfg.yaml \
  /var/lib/k180 \
  /var/lib/k180/firmware_ver_info.json \
  /var/lib/k180/api_ver_info.json \
  /var/lib/k180/tmp_fw \
  /var/lib/k180/tmp_rec_zip \
  /var/lib/k180/tmp_rec_zip/zip_batch.lock \
  /usr/local/share/k180/yolov8n_fp16.engine \
  /usr/local/bin/grand_yeah \
  /usr/local/bin/restful_api_server \
  /usr/local/libexec/k180 \
  /usr/local/libexec/k180/gen_fw_dec \
  /usr/local/libexec/k180/modify_sysip_netplan \
  /usr/local/libexec/k180/reboot_system \
  /usr/local/libexec/k180/restart_program_camera \
  /var/www/private \
  /var/www/private/users.json \
  /data
```

## 未來只更新某個執行檔

### 更新 main program

建議正式流程仍走 firmware package：內部用 `gen_fw_enc_*` 產生 `.hhc`，客戶透過 web upload，產品機由 `/usr/local/libexec/k180/gen_fw_dec` 更新：

- `/usr/local/bin/grand_yeah`
- `/usr/local/bin/restful_api_server`

這個流程會先解到 `/var/lib/k180/fw_update.XXXXXX`，再用 temporary file 與 `rename()` 做原子替換。

### 現場維護時手動更新單一 binary

若工程人員直接維護，可以只安裝單一檔：

```bash
sudo install -m 0755 -o root -g root ./grand_yeah /usr/local/bin/grand_yeah
sudo systemctl restart grand_yeah.service
```

```bash
sudo install -m 0755 -o root -g root ./restful_api_server /usr/local/bin/restful_api_server
sudo systemctl restart restful_api.service
```

```bash
sudo install -m 0750 -o root -g k180 ./modify_sysip_netplan /usr/local/libexec/k180/modify_sysip_netplan
```

### 建議的 release 分類

1. Full install package：第一次部署或重灌使用，包含全部檔案。
2. Firmware update package：給客戶 web upload 使用，包含 `grand_yeah` 與 `restful_api_server` 的 `.hhc`。
3. Service/helper hotfix package：給工程人員維護用，只包含某支 helper 或 systemd/sudoers 修正。

不要把 `gen_fw_enc_*` 放到產品機；它只應該留在內部 release/build machine。
