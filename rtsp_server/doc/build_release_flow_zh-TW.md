# Build 與正式版發佈流程

本專案會從同一份 source tree build 出四種 `grand_yeah` profile：

```text
build/grand_yeah_fps60_short
build/grand_yeah_fps60_long
build/grand_yeah_fps30_short
build/grand_yeah_fps30_long
```

客戶機器部署時只會放其中一個 binary；但開發時可以一次 build 四種。

## 開發版 Build

開發時直接使用一般 `make` 指令即可。開發版不需要 git tag，也允許在 working tree 還沒整理乾淨時 build。

Build 四種 profile：

```bash
make
```

只 build 一種 profile：

```bash
make fps60-short
make fps60-long
make fps30-short
make fps30-long
```

查看 build 時會產生的版本資訊 header：

```bash
make print-build-info
```

開發版 build info 會類似：

```cpp
#define K180_FW_VER "dev"
#define K180_BUILD_MODE "dev"
#define K180_GIT_COMMIT "10bd6b747c0a"
#define K180_GIT_DESCRIBE "v1.60.12-3-g10bd6b7-dirty"
#define K180_GIT_DIRTY 1
```

## 正式版 Build

正式版 build 會比較嚴格：

```text
1. working tree 必須是乾淨的。
2. HEAD 必須有 vX.Y.Z 格式的 git tag。
3. 韌體版本號會從 git tag 取得。
4. 一次只 build 一個 PROFILE。
```

如果目前 `HEAD` 已經有 tag，可以直接 build 正式版 binary：

```bash
make release PROFILE=fps60-short
```

其他合法 profile：

```bash
make release PROFILE=fps60-long
make release PROFILE=fps30-short
make release PROFILE=fps30-long
```

下一次正式版發佈時，先 commit，再建立下一個 tag，最後 build：

```bash
git add Makefile src/gy_two_cam.cpp cfg/firmware_ver_info.json prepare_release.sh doc/build_release_flow.md doc/build_release_flow_zh-TW.md
git commit -m "Update firmware build and release flow"

./prepare_release.sh patch
make release PROFILE=fps60-short
```

版本遞增方式：

```bash
./prepare_release.sh patch   # v1.60.12 -> v1.60.13
./prepare_release.sh minor   # v1.60.12 -> v1.61.0
./prepare_release.sh major   # v1.60.12 -> v2.0.0
./prepare_release.sh 1.60.20 # 明確指定版本
```

只預覽下一版，不建立 tag：

```bash
./prepare_release.sh --dry-run patch
```

確認正式版沒問題後，再 push source 和 tag：

```bash
git push
git push origin v1.60.13
```

## Generated Header

Makefile 會在 build 時產生這個檔案：

```text
build/generated/k180_build_info.h
```

這是 build output，不需要 commit。`.gitignore` 已經用 `build/` 排除它。

generated header 會包含 git 與 release metadata：

```cpp
#define K180_FW_VER "1.60.12"
#define K180_BUILD_MODE "release"
#define K180_GIT_COMMIT "10bd6b747c0a"
#define K180_GIT_DESCRIBE "v1.60.12"
#define K180_GIT_DIRTY 0
```

`src/gy_two_cam.cpp` 會 include：

```cpp
#include "k180_build_info.h"
```

## FPS Profile 邏輯

generated header 不包含 FPS profile，這是刻意設計。

FPS profile 仍然由 `Makefile` 裡每個 profile 的 compiler flags 決定：

```make
CFLAGS_FPS60 = -DK180_PROFILE_NAME=\"fps60\" -DK180_TRIGGER_INTERVAL_US=16667 -DK180_MAX_STREAM_FPS=60
CFLAGS_FPS30 = -DK180_PROFILE_NAME=\"fps30\" -DK180_TRIGGER_INTERVAL_US=33334 -DK180_MAX_STREAM_FPS=30
```

`src/gy_two_cam.cpp` 會從 `K180_MAX_STREAM_FPS` 推導出 `FPS_VER`：

```cpp
#define FPS_VER K180_STRINGIFY(K180_MAX_STREAM_FPS)
```

所以即使 `make` 一次 build 四個 dev binary，四者共用同一份 build-info header 也沒問題：

```text
四個 profile 共用：
  K180_FW_VER
  K180_BUILD_MODE
  K180_GIT_COMMIT
  K180_GIT_DESCRIBE
  K180_GIT_DIRTY

每個 profile 各自不同：
  K180_PROFILE_NAME
  K180_TRIGGER_INTERVAL_US
  K180_MAX_STREAM_FPS
  FPS_VER
```

## Runtime Firmware Info 檔案

`grand_yeah` 啟動後會寫入：

```text
/var/lib/k180/firmware_ver_info.json
```

這個檔案的初始範本來自：

```text
cfg/firmware_ver_info.json
```

`grand_yeah` 會更新 `sysinfo`：

```json
{
  "sysinfo": {
    "fwver": "1.60.12",
    "fpsver": "60",
    "max_stream_fps": 60,
    "build_mode": "release",
    "git_commit": "10bd6b747c0a",
    "git_describe": "v1.60.12",
    "git_dirty": 0
  }
}
```

Web UI 和 REST API 會讀 `sysinfo.max_stream_fps`，用來決定是否允許設定 FPS 60。

## Release Helper 邏輯

`prepare_release.sh` 使用本機 git tag，不需要 GitHub 網路連線。

它會尋找符合以下格式的 tag：

```text
vX.Y.Z
```

然後依照 `patch`、`minor`、`major` 自動計算下一版，也可以接受明確指定的版本號。

以下情況會拒絕建立 tag：

```text
1. working tree 還有未 commit 的變更。
2. 目標 tag 已經存在。
3. 明確指定的版本沒有大於目前最新 release tag。
```

`make release PROFILE=...` 會接著檢查：

```text
1. BUILD_MODE 必須是 release。
2. FW_VER 必須能從目前 HEAD 的 exact git tag 解析出來。
3. working tree 必須乾淨。
4. HEAD 必須剛好被標記為 vFW_VER。
5. PROFILE 必須是 fps60-short、fps60-long、fps30-short、fps30-long 其中之一。
```

## 常見情境

第一次正式版，如果 repo 還沒有任何 tag：

```bash
./prepare_release.sh 1.60.12
make release PROFILE=fps60-short
```

下一個 patch 正式版：

```bash
git add ...
git commit -m "..."
./prepare_release.sh patch
make release PROFILE=fps60-short
```

Build 暫時測試用的 debug binary：

```bash
make fps60-short
```

確認目前會編進 binary 的 metadata：

```bash
make print-build-info
```
