#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  ./prepare_release.sh [--dry-run] patch|minor|major
  ./prepare_release.sh [--dry-run] X.Y.Z

Examples:
  ./prepare_release.sh --dry-run patch
  ./prepare_release.sh patch
  ./prepare_release.sh 1.60.12
EOF
}

dry_run=0
if [[ "${1:-}" == "--dry-run" ]]; then
    dry_run=1
    shift
fi

if [[ $# -ne 1 ]]; then
    usage
    exit 2
fi

kind="$1"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "ERROR: this script must run inside a git repository." >&2
    exit 1
fi

latest_tag="$(git tag --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=-v:refname | head -n 1)"
latest_ver="${latest_tag#v}"
if [[ -z "$latest_tag" ]]; then
    latest_ver="0.0.0"
fi

if [[ "$latest_ver" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    patch="${BASH_REMATCH[3]}"
else
    echo "ERROR: latest release tag '$latest_tag' is not vX.Y.Z." >&2
    exit 1
fi

case "$kind" in
    patch)
        patch=$((patch + 1))
        ;;
    minor)
        minor=$((minor + 1))
        patch=0
        ;;
    major)
        major=$((major + 1))
        minor=0
        patch=0
        ;;
    v*)
        kind="${kind#v}"
        if [[ ! "$kind" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            echo "ERROR: explicit version must be X.Y.Z or vX.Y.Z." >&2
            exit 1
        fi
        next_ver="$kind"
        ;;
    *)
        if [[ ! "$kind" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            echo "ERROR: argument must be patch, minor, major, or X.Y.Z." >&2
            exit 1
        fi
        next_ver="$kind"
        ;;
esac

if [[ -z "${next_ver:-}" ]]; then
    next_ver="${major}.${minor}.${patch}"
fi

if [[ "$next_ver" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    next_major="${BASH_REMATCH[1]}"
    next_minor="${BASH_REMATCH[2]}"
    next_patch="${BASH_REMATCH[3]}"
else
    echo "ERROR: next version '$next_ver' is not X.Y.Z." >&2
    exit 1
fi

if [[ -n "$latest_tag" ]]; then
    if (( next_major < major )) ||
       (( next_major == major && next_minor < minor )) ||
       (( next_major == major && next_minor == minor && next_patch <= patch )); then
        echo "ERROR: next version ${next_ver} must be greater than latest ${latest_ver}." >&2
        exit 1
    fi
fi

next_tag="v${next_ver}"

if git rev-parse -q --verify "refs/tags/${next_tag}" >/dev/null; then
    echo "ERROR: tag ${next_tag} already exists." >&2
    exit 1
fi

echo "Latest release tag : ${latest_tag:-<none>}"
echo "Next release tag   : ${next_tag}"

if [[ "$dry_run" -eq 1 ]]; then
    echo "Dry run only. No tag created."
    exit 0
fi

if [[ -n "$(git status --porcelain)" ]]; then
    echo "ERROR: working tree is not clean. Commit or stash changes before preparing a release." >&2
    git status --short
    exit 1
fi

git tag -a "$next_tag" -m "K180 firmware ${next_tag}"

cat <<EOF
Created ${next_tag}.

Next commands:
  make release PROFILE=fps60-short
  git push
  git push origin ${next_tag}
EOF
