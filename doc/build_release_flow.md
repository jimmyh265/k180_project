# Build and Release Flow

This project builds four `grand_yeah` profiles from the same source tree:

```text
build/grand_yeah_fps60_short
build/grand_yeah_fps60_long
build/grand_yeah_fps30_short
build/grand_yeah_fps30_long
```

Only one binary is deployed to a customer machine, but development builds can build all four.

## Development Build

Use normal `make` commands. Development builds do not require a git tag and may be built from a dirty working tree.

Build all four profiles:

```bash
make
```

Build one profile:

```bash
make fps60-short
make fps60-long
make fps30-short
make fps30-long
```

Inspect the generated build info header:

```bash
make print-build-info
```

Development build info looks like:

```cpp
#define K180_FW_VER "dev"
#define K180_BUILD_MODE "dev"
#define K180_GIT_COMMIT "10bd6b747c0a"
#define K180_GIT_DESCRIBE "v1.60.12-3-g10bd6b7-dirty"
#define K180_GIT_DIRTY 1
```

## Release Build

Release builds are intentionally stricter:

```text
1. The working tree must be clean.
2. HEAD must be tagged with vX.Y.Z.
3. The firmware version comes from that git tag.
4. One PROFILE is built at a time.
```

If `HEAD` is already tagged, build the release binary directly:

```bash
make release PROFILE=fps60-short
```

Other valid profiles:

```bash
make release PROFILE=fps60-long
make release PROFILE=fps30-short
make release PROFILE=fps30-long
```

For the next release, commit first, then create the next tag, then build:

```bash
git add Makefile src/gy_two_cam.cpp cfg/firmware_ver_info.json prepare_release.sh doc/build_release_flow.md
git commit -m "Update firmware build and release flow"

./prepare_release.sh patch
make release PROFILE=fps60-short
```

Version bump options:

```bash
./prepare_release.sh patch   # v1.60.12 -> v1.60.13
./prepare_release.sh minor   # v1.60.12 -> v1.61.0
./prepare_release.sh major   # v1.60.12 -> v2.0.0
./prepare_release.sh 1.60.20 # explicit version
```

Preview the next version without creating a tag:

```bash
./prepare_release.sh --dry-run patch
```

After release validation, push source and tag when ready:

```bash
git push
git push origin v1.60.13
```

## Generated Header

The Makefile generates this file at build time:

```text
build/generated/k180_build_info.h
```

It is a build output and should not be committed. It is covered by `build/` in `.gitignore`.

The generated header contains git and release metadata:

```cpp
#define K180_FW_VER "1.60.12"
#define K180_BUILD_MODE "release"
#define K180_GIT_COMMIT "10bd6b747c0a"
#define K180_GIT_DESCRIBE "v1.60.12"
#define K180_GIT_DIRTY 0
```

`src/gy_two_cam.cpp` includes it with:

```cpp
#include "k180_build_info.h"
```

## FPS Profile Logic

The generated header does not contain the FPS profile. This is intentional.

The FPS profile is still decided by the per-profile compiler flags in `Makefile`:

```make
CFLAGS_FPS60 = -DK180_PROFILE_NAME=\"fps60\" -DK180_TRIGGER_INTERVAL_US=16667 -DK180_MAX_STREAM_FPS=60
CFLAGS_FPS30 = -DK180_PROFILE_NAME=\"fps30\" -DK180_TRIGGER_INTERVAL_US=33334 -DK180_MAX_STREAM_FPS=30
```

`src/gy_two_cam.cpp` derives `FPS_VER` from `K180_MAX_STREAM_FPS`:

```cpp
#define FPS_VER K180_STRINGIFY(K180_MAX_STREAM_FPS)
```

So a full `make` can safely build all four dev binaries with one shared build-info header:

```text
Shared across all four:
  K180_FW_VER
  K180_BUILD_MODE
  K180_GIT_COMMIT
  K180_GIT_DESCRIBE
  K180_GIT_DIRTY

Different per profile:
  K180_PROFILE_NAME
  K180_TRIGGER_INTERVAL_US
  K180_MAX_STREAM_FPS
  FPS_VER
```

## Firmware Info Runtime File

At runtime, `grand_yeah` writes:

```text
/var/lib/k180/firmware_ver_info.json
```

The file is initialized from:

```text
cfg/firmware_ver_info.json
```

`grand_yeah` updates `sysinfo` with:

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

The web UI and REST API use `sysinfo.max_stream_fps` to decide whether FPS 60 is allowed.

## Release Helper Logic

`prepare_release.sh` uses local git tags. It does not need GitHub network access.

It searches for tags matching:

```text
vX.Y.Z
```

Then it computes the next version from `patch`, `minor`, or `major`, or accepts an explicit version.

It refuses to create a tag when:

```text
1. The working tree is dirty.
2. The target tag already exists.
3. The explicit target version is not greater than the latest release tag.
```

`make release PROFILE=...` then verifies:

```text
1. BUILD_MODE is release.
2. FW_VER can be resolved from the exact git tag on HEAD.
3. The working tree is clean.
4. HEAD is exactly tagged as vFW_VER.
5. PROFILE is one of fps60-short, fps60-long, fps30-short, fps30-long.
```

## Common Cases

First release when no tag exists:

```bash
./prepare_release.sh 1.60.12
make release PROFILE=fps60-short
```

Next patch release:

```bash
git add ...
git commit -m "..."
./prepare_release.sh patch
make release PROFILE=fps60-short
```

Build a temporary debug binary:

```bash
make fps60-short
```

Confirm what metadata will be compiled in:

```bash
make print-build-info
```
