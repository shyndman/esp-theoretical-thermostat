#!/usr/bin/env bash

start_change() {
  if [[ $# -ne 1 ]]; then
    echo "start-change: usage start_change <change-id>" >&2
    return 1
  }

  local change_id="$1"
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  local repo_root
  repo_root="$(cd "${script_dir}/.." && pwd)"

  if [[ "${PWD}" != "${repo_root}" ]]; then
    echo "start-change: run from repo root (${repo_root})" >&2
    return 1
  fi

  if [[ ! -d "${repo_root}/openspec/changes/${change_id}" ]]; then
    echo "start-change: openspec/changes/${change_id} does not exist" >&2
    return 1
  fi

  local target_worktree="${repo_root}/worktrees/${change_id}"
  local created_worktree=0
  if [[ -d "${target_worktree}" ]]; then
    echo "start-change: worktree worktrees/${change_id} already exists; skipping creation" >&2
  else
    echo "start-change: creating worktree worktrees/${change_id}" >&2
    local branch_args=(-b "${change_id}" main)
    if git rev-parse --verify "refs/heads/${change_id}" >/dev/null 2>&1; then
      branch_args=("${change_id}")
    fi
    if ! git worktree add "worktrees/${change_id}" "${branch_args[@]}"; then
      echo "start-change: git worktree add failed" >&2
      return 1
    fi
    created_worktree=1
  fi

  cd "${target_worktree}" || return 1

  if (( created_worktree )); then
    if [[ ! -f "../../sdkconfig" ]]; then
      echo "start-change: ../../sdkconfig not found" >&2
      return 1
    fi
    cp ../../sdkconfig ./
    echo "start-change: copied sdkconfig into worktree" >&2
  fi
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "start-change: source this file (e.g. 'source scripts/start-change.sh') to define start_change" >&2
  exit 1
fi
