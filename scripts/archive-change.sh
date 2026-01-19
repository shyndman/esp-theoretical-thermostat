#!/usr/bin/env zsh

if [[ -z "${ZSH_VERSION:-}" ]]; then
  echo "archive-change: source this file from zsh" >&2
  return 1 2>/dev/null || exit 1
fi

if [[ -z "${_ARCHIVE_CHANGE_SCRIPT_PATH:-}" ]]; then
  typeset -g _ARCHIVE_CHANGE_SCRIPT_PATH="${(%):-%N}"
  if [[ -z "${_ARCHIVE_CHANGE_SCRIPT_PATH}" || "${_ARCHIVE_CHANGE_SCRIPT_PATH}" == "zsh" ]]; then
    echo "archive-change: unable to determine script path" >&2
    return 1 2>/dev/null || exit 1
  fi
fi

if [[ -z "${_ARCHIVE_CHANGE_REPO_ROOT:-}" ]]; then
  typeset -g _ARCHIVE_CHANGE_REPO_ROOT="${_ARCHIVE_CHANGE_SCRIPT_PATH:A:h:h}"
fi

archive_change() {
  emulate -L zsh -o pipefail

  local original_pwd="${PWD}"
  trap 'builtin cd -- "${original_pwd}" >/dev/null 2>&1' RETURN

  local commit_message=""
  if [[ $# -gt 0 ]]; then
    commit_message="$*"
  fi

  local repo_root="${_ARCHIVE_CHANGE_REPO_ROOT}"
  if git -C "${PWD}" rev-parse --show-toplevel >/dev/null 2>&1; then
    repo_root="$(git -C "${PWD}" rev-parse --show-toplevel)"
  fi
  builtin cd -- "${repo_root}" || return 1

  local worktree_root_name="$(basename "${repo_root}")"
  if [[ "${worktree_root_name}" == "esp-theoretical-thermostat" ]]; then
    echo "archive-change: this script should be run from a change worktree, not the main worktree" >&2
    return 1
  fi

  local change_branch="$(git rev-parse --abbrev-ref HEAD)"
  if [[ "${change_branch}" == "main" ]]; then
    echo "archive-change: refusing to operate on main branch" >&2
    return 1
  fi

  if [[ -z "${commit_message}" ]]; then
    commit_message="Archive ${change_branch}"
  fi

  if ! git diff --quiet --ignore-submodules HEAD --; then
    echo "archive-change: working tree has uncommitted changes" >&2
    return 1
  fi

  if [[ -n "$(git status --short --untracked-files=normal)" ]]; then
    echo "archive-change: working tree has untracked changes" >&2
    return 1
  fi

  echo "archive-change: rebasing ${change_branch} onto main"
  git rebase main

  local main_worktree="$(cd "${repo_root}/../.." && pwd)"
  if [[ ! -d "${main_worktree}/.git" ]]; then
    echo "archive-change: could not locate main worktree at ${main_worktree}" >&2
    return 1
  fi

  local main_head="$(git -C "${main_worktree}" rev-parse --abbrev-ref HEAD)"
  if [[ "${main_head}" != "main" ]]; then
    echo "archive-change: main worktree must have main checked out (currently ${main_head})" >&2
    return 1
  fi

  echo "archive-change: fast-forwarding main with ${change_branch}"
  git -C "${main_worktree}" merge --ff-only "${change_branch}"

  builtin cd -- "${main_worktree}" || return 1

  local main_head_after="$(git rev-parse --abbrev-ref HEAD)"
  if [[ "${main_head_after}" != "main" ]]; then
    echo "archive-change: expected to be on main in ${main_worktree}" >&2
    return 1
  fi

  local archive_cmd="Archive ${change_branch}, without any plan confirmations. Instructions:

$(cat ~/.codex/prompts/openspec-archive.md | sed "s/\$ARGUMENTS/$change_branch/g")"

  echo "archive-change: running codex exec \"${archive_cmd}\""
  codex exec "${archive_cmd}"

  echo "archive-change: committing archive results"
  git add openspec
  git commit -m "${commit_message}"

  echo "archive-change: removing worktree ${repo_root}"
  if ! git -C "${main_worktree}" worktree remove "${repo_root}"; then
    echo "archive-change: failed to remove worktree ${repo_root}" >&2
    return 1
  fi

  echo "archive-change: deleting branch ${change_branch}"
  if ! git -C "${main_worktree}" branch -D "${change_branch}"; then
    echo "archive-change: failed to delete branch ${change_branch}" >&2
    return 1
  fi

  echo "archive-change: done"
}

if [[ "${zsh_eval_context:-}" != *file* ]]; then
  echo "archive-change: source this file (e.g. 'source scripts/archive-change.sh') to define archive_change" >&2
  return 1 2>/dev/null || exit 1
fi
