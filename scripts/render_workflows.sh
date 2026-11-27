#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Render check: every workflow in workflows/ must evaluate with jsonnet. Most
# workflows are not run in CI, so a renamed lib.libsonnet function or a typo in
# one of them would otherwise go unnoticed until somebody runs it. External
# variables and required top-level arguments get the placeholder value 1,
# which is valid both where a workflow parses a number and where it expects a
# file name. Top-level arguments with a default keep it.
set -uo pipefail

workflows="$(cd "$(dirname "$0")/../workflows" && pwd)"
failed=0
count=0

for workflow in "$workflows"/*.jsonnet; do
  args=()
  while read -r var; do
    args+=(--ext-str "$var=1")
  done < <(grep -o "std\.extVar('[^']*')" "$workflow" | sed "s/.*'\(.*\)'.*/\1/" | sort -u)
  # Required top-level arguments: the parameters of `function(...)` at the
  # start of a line that have no default. File names are passed as strings,
  # everything else as a number.
  while read -r arg; do
    case "$arg" in
      *file) args+=(--tla-str "$arg=1") ;;
      *) args+=(--tla-code "$arg=1") ;;
    esac
  done < <(sed -n 's/^function(\(.*\)).*/\1/p' "$workflow" | tr ',' '\n' | grep -v '=' | tr -d ' ' | grep .)
  if ! jsonnet -J "$workflows" "${args[@]}" "$workflow" >/dev/null; then
    echo "FAIL: $(basename "$workflow") does not render"
    failed=1
  fi
  count=$((count + 1))
done

[ "$count" -gt 0 ] || {
  echo "FAIL: no workflows found in $workflows"
  exit 1
}
[ "$failed" -eq 0 ] && echo "PASS: $count workflows render"
exit "$failed"
