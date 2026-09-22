#!/usr/bin/env bash
#
# run_firmware_build.sh — build the Evrahim S3 firmware in GitHub Actions
# and download the flashable binary as an artifact.
#
# Usage:
#   ./tools/run_firmware_build.sh [--branch BRANCH] [--out DIR] [--no-wait]
#
# Examples:
#   ./tools/run_firmware_build.sh                        # build current branch, wait, download to ./firmware/
#   ./tools/run_firmware_build.sh --no-wait              # just trigger the run and print its URL
#   ./tools/run_firmware_build.sh --branch Mobile-test --out /tmp/fw
#
# Requirements: GitHub CLI (gh) installed and authenticated (gh auth login).
#
set -euo pipefail

WORKFLOW="build-evrahim-s3.yml"
ARTIFACT="Bruce-evrahim-s3"
BIN_NAME="Bruce-evrahim-s3.bin"
BRANCH="$(git branch --show-current 2>/dev/null || echo Mobile-test)"
OUT_DIR="firmware"
WAIT=1

usage() { sed -n '2,/^#$/p' "$0" | sed 's/^# \?//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --branch) BRANCH="$2"; shift 2 ;;
        --out) OUT_DIR="$2"; shift 2 ;;
        --no-wait) WAIT=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

command -v gh >/dev/null 2>&1 || { echo "ERROR: 'gh' CLI not found. Install it: https://cli.github.com" >&2; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "ERROR: 'gh' is not authenticated. Run: gh auth login" >&2; exit 1; }

echo "▶ Triggering workflow '$WORKFLOW' on branch '$BRANCH'..."
TRIGGERED_AT=$(date -u +%s)
gh workflow run "$WORKFLOW" --ref "$BRANCH"

# The run appears a few seconds after the dispatch; poll for the newest one.
RUN_ID=""
for i in $(seq 1 12); do
    sleep 5
    RUN_ID="$(gh run list --workflow "$WORKFLOW" --branch "$BRANCH" --limit 5 \
        --json databaseId,createdAt \
        --jq --argjson since "$TRIGGERED_AT" \
        '[.[] | select((.createdAt | fromdateiso8601) >= $since)] | first | .databaseId // empty' \
        2>/dev/null || true)"
    if [[ -n "$RUN_ID" ]]; then break; fi
done
if [[ -z "$RUN_ID" ]]; then
    echo "ERROR: no new workflow run appeared for branch '$BRANCH'. Check 'gh run list'." >&2
    exit 1
fi

RUN_URL="$(gh run view "$RUN_ID" --json url --jq .url)"
echo "▶ Run started: $RUN_URL"

if [[ "$WAIT" -eq 0 ]]; then
    echo "Triggered only (--no-wait). Download the artifact later with:"
    echo "  gh run download $RUN_ID -n $ARTIFACT -D $OUT_DIR"
    exit 0
fi

echo "▶ Waiting for the build to finish (takes ~5-15 min on first run)..."
gh run watch "$RUN_ID" --exit-status

echo "▶ Downloading artifact '$ARTIFACT' to '$OUT_DIR/'..."
rm -rf "$OUT_DIR"
gh run download "$RUN_ID" -n "$ARTIFACT" -D "$OUT_DIR"

BIN_PATH="$OUT_DIR/$BIN_NAME"
if [[ ! -f "$BIN_PATH" ]]; then
    echo "ERROR: expected $BIN_PATH not found after download." >&2
    exit 1
fi

echo ""
echo "✅ Firmware ready: $BIN_PATH ($(du -h "$BIN_PATH" | cut -f1))"
sha256sum "$BIN_PATH"
echo ""
echo "Flash it with:"
echo "  esptool.py --port /dev/ttyACM0 write_flash 0x00000 $BIN_PATH"
