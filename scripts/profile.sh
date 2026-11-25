#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Profiling runs behind the `profile-*` pixi tasks:
#   pixi run -e bench profile-flamegraph
#   TRACE_WORKFLOW=gun_mt_bench TRACE_EVENTS=200 pixi run -e bench profile-trace
# See docs/benchmarks.md.

set -euo pipefail

PROFILE_WORKFLOW=${PROFILE_WORKFLOW:-gun_st_bench}
PROFILE_EVENTS=${PROFILE_EVENTS:-500}
PROFILE_FREQ=${PROFILE_FREQ:-99}
TRACE_WORKFLOW=${TRACE_WORKFLOW:-gun_st_bench}
TRACE_EVENTS=${TRACE_EVENTS:-50}

usage() {
  cat >&2 <<EOF
Usage: $0 <profile>

Profiles:
  flamegraph  sampled CPU profile with perf; writes an SVG flame graph
              if flamegraph.pl and stackcollapse-perf.pl are on PATH
  trace       Chrome trace of a short run (needs -DAEGIR_ENABLE_TRACE=ON)
EOF
  exit 2
}

[[ $# -eq 1 ]] || usage

case $1 in
flamegraph)
  perf record -F "$PROFILE_FREQ" -g --call-graph=dwarf -o perf.data -- \
    phlex -c <(jsonnet --ext-str "events=$PROFILE_EVENTS" \
      "workflows/$PROFILE_WORKFLOW.jsonnet") -j 1
  if command -v flamegraph.pl >/dev/null && command -v stackcollapse-perf.pl >/dev/null; then
    perf script -i perf.data | stackcollapse-perf.pl | flamegraph.pl \
      >"flamegraph_$PROFILE_WORKFLOW.svg"
    echo "Wrote flamegraph_$PROFILE_WORKFLOW.svg"
  else
    echo "flamegraph.pl not in PATH, printing perf report instead"
    perf report -i perf.data --stdio | head -80
    echo ""
    echo "(install flamegraph for SVG output: nix-shell -p flamegraph)"
  fi
  ;;
trace)
  AEGIR_TRACE_FILE="trace_$TRACE_WORKFLOW.json" \
    phlex -c <(jsonnet --ext-str "events=$TRACE_EVENTS" \
      "workflows/$TRACE_WORKFLOW.jsonnet") -j 1
  echo "Wrote trace_$TRACE_WORKFLOW.json, open it in https://ui.perfetto.dev"
  ;;
*)
  usage
  ;;
esac
