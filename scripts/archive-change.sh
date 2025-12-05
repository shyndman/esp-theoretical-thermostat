#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <commit-message>" >&2
  exit 1
}

if [[ $# -eq 0 ]]; then
  usage
fi

COMMIT_MESSAGE="$*"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

WORKTREE_ROOT_NAME="$(basename "${REPO_ROOT}")"
if [[ "${WORKTREE_ROOT_NAME}" == "esp-theoretical-thermostat" ]]; then
  echo "archive-change: this script should be run from a change worktree, not the main worktree" >&2
  exit 1
fi

CHANGE_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [[ "${CHANGE_BRANCH}" == "main" ]]; then
  echo "archive-change: refusing to operate on main branch" >&2
  exit 1
fi

if ! git diff --quiet --ignore-submodules HEAD --; then
  echo "archive-change: working tree has uncommitted changes" >&2
  exit 1
fi

if [[ -n "$(git status --short --untracked-files=normal)" ]]; then
  echo "archive-change: working tree has untracked changes" >&2
  exit 1
fi

echo "archive-change: rebasing ${CHANGE_BRANCH} onto main"
git rebase main

MAIN_WORKTREE="$(cd "${REPO_ROOT}/../.." && pwd)"
if [[ ! -d "${MAIN_WORKTREE}/.git" ]]; then
  echo "archive-change: could not locate main worktree at ${MAIN_WORKTREE}" >&2
  exit 1
fi

MAIN_HEAD="$(git -C "${MAIN_WORKTREE}" rev-parse --abbrev-ref HEAD)"
if [[ "${MAIN_HEAD}" != "main" ]]; then
  echo "archive-change: main worktree must have main checked out (currently ${MAIN_HEAD})" >&2
  exit 1
fi

echo "archive-change: fast-forwarding main with ${CHANGE_BRANCH}"
git -C "${MAIN_WORKTREE}" merge --ff-only "${CHANGE_BRANCH}"

cd "${MAIN_WORKTREE}"

MAIN_HEAD_AFTER="$(git rev-parse --abbrev-ref HEAD)"
if [[ "${MAIN_HEAD_AFTER}" != "main" ]]; then
  echo "archive-change: expected to be on main in ${MAIN_WORKTREE}" >&2
  exit 1
fi

ARCHIVE_CMD="Archive ${CHANGE_BRANCH}, without any plan confirmations. Instructions:

$(cat ~/.codex/prompts/openspec-archive.md | sed "s/\$ARGUMENTS/$CHANGE_BRANCH/g")"

echo "archive-change: running codex exec \"${ARCHIVE_CMD}\""
codex exec "${ARCHIVE_CMD}"

echo "archive-change: committing archive results"
git add openspec
git commit -m "${COMMIT_MESSAGE}"

echo "archive-change: done"
