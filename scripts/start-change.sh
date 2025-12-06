#!/usr/bin/env zsh

if [[ -z "${ZSH_VERSION:-}" ]]; then
  echo "start-change: source this file from zsh" >&2
  return 1 2>/dev/null || exit 1
fi

if [[ -z "${_START_CHANGE_SCRIPT_PATH:-}" ]]; then
  typeset -g _START_CHANGE_SCRIPT_PATH="${(%):-%N}"
  if [[ -z "${_START_CHANGE_SCRIPT_PATH}" || "${_START_CHANGE_SCRIPT_PATH}" == "zsh" ]]; then
    echo "start-change: unable to determine script path" >&2
    return 1 2>/dev/null || exit 1
  fi
fi

if [[ -z "${_START_CHANGE_REPO_ROOT:-}" ]]; then
  typeset -g _START_CHANGE_REPO_ROOT="${_START_CHANGE_SCRIPT_PATH:A:h:h}"
fi

if [[ -z "${_START_CHANGE_WORKTREES_DIR:-}" ]]; then
  typeset -g _START_CHANGE_WORKTREES_DIR="${_START_CHANGE_REPO_ROOT}/worktrees"
fi

start_change() {
  emulate -L zsh -o pipefail

  if [[ $# -ne 1 ]]; then
    echo "start-change: usage start_change <change-id>" >&2
    return 1
  fi

  local change_id="$1"
  local repo_root="${_START_CHANGE_REPO_ROOT}"
  local change_spec_dir="${repo_root}/openspec/changes/${change_id}"

  if [[ ! -d "${change_spec_dir}" ]]; then
    echo "start-change: openspec/changes/${change_id} does not exist" >&2
    return 1
  fi

  local target_worktree="${_START_CHANGE_WORKTREES_DIR}/${change_id}"
  local created_worktree=0

  if [[ -d "${target_worktree}" ]]; then
    echo "start-change: worktree worktrees/${change_id} already exists; skipping creation" >&2
  else
    echo "start-change: creating worktree worktrees/${change_id}" >&2
    local -a branch_args
    branch_args=(-b "${change_id}" main)
    if git -C "${repo_root}" rev-parse --verify "refs/heads/${change_id}" >/dev/null 2>&1; then
      branch_args=("${change_id}")
    fi
    if ! git -C "${repo_root}" worktree add "${target_worktree}" "${branch_args[@]}"; then
      echo "start-change: git worktree add failed" >&2
      return 1
    fi
    created_worktree=1
  fi

  builtin cd -- "${target_worktree}" || return 1

  if (( created_worktree )); then
    local source_sdkconfig="${repo_root}/sdkconfig"
    if [[ ! -f "${source_sdkconfig}" ]]; then
      echo "start-change: ${source_sdkconfig} not found" >&2
      return 1
    fi
    cp "${source_sdkconfig}" ./
    ln -s ./AGENTS.md ./CLAUDE.md
    echo "start-change: copied sdkconfig into worktree" >&2
  fi
}

if [[ "${zsh_eval_context:-}" != *file* ]]; then
  echo "start-change: source this file (e.g. 'source scripts/start-change.sh') to define start_change" >&2
  return 1 2>/dev/null || exit 1
fi
