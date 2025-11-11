#!/usr/bin/env bash
set -euo pipefail

# Ensure we run from the repository root so relative paths (like sdkconfig) resolve.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

if [ -f sdkconfig ]; then
    echo "init-worktree: Found existing sdkconfig; nothing to do."
    exit 0
fi

if [ -z "${IDF_PATH:-}" ]; then
    echo "init-worktree: IDF_PATH is not set; skipping worktree preparation."
    exit 0
fi

EXPORT_SH="${IDF_PATH%/}/export.sh"
if [ ! -f "${EXPORT_SH}" ]; then
    echo "init-worktree: ${EXPORT_SH} not found; skipping worktree preparation."
    exit 0
fi

echo "init-worktree: sourcing ${EXPORT_SH}" >&2
# shellcheck disable=SC1090
. "${EXPORT_SH}"

echo "init-worktree: running idf.py reconfigure" >&2
idf.py reconfigure
