#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Benchmarks behind the `bench-*` pixi tasks. Run them through pixi, e.g.
#   pixi run -e bench bench-geant4
#   G4_EVENTS=500 pixi run -e bench bench-geant4-threads
# The environment variables below override the defaults. See docs/benchmarks.md.

set -euo pipefail

BUILD_DIR=${AEGIR_BUILD_DIR:-build}
EVENTS=${EVENTS:-10000}
G4_EVENTS=${G4_EVENTS:-100}
G4_CONCURRENCY=${G4_CONCURRENCY:-4}
BENCH_WORKFLOW=${BENCH_WORKFLOW:-gun_mt}
BENCH_G4_THREADS=${BENCH_G4_THREADS:-"1 2 4 8"}
BENCH_PARALLELISM=${BENCH_PARALLELISM:-"1 2 4 8"}
BENCH_REPEATS=${BENCH_REPEATS:-5}
BENCH_SWEEP_EVENTS=${BENCH_SWEEP_EVENTS:-1000}

standalone="./${BUILD_DIR}/pythia8_benchmark"

# phlex command line for a workflow, with jsonnet --ext-str arguments.
phlex_cmd() {
  local workflow=$1
  shift
  local ext=""
  for kv in "$@"; do ext+=" --ext-str $kv"; done
  printf 'phlex -c <(jsonnet%s workflows/%s.jsonnet)' "$ext" "$workflow"
}

# The phlex commands use process substitution, so hyperfine has to run them
# with bash rather than its default sh.
hf() { hyperfine --shell=bash "$@"; }

usage() {
  cat >&2 <<EOF
Usage: $0 <benchmark>

Benchmarks:
  pythia8          standalone Pythia8 vs phlex workflows
  standalone       standalone Pythia8, serial vs parallel
  phlex-io         phlex I/O overhead: noop vs mc_only
  fairship         default vs FairShip-like Pythia8 configuration
  scaling          event-count sweep (start-up vs per-event cost)
  geant4           Geant4 single- vs multi-threaded (no output)
  geant4-io        Geant4 with and without output
  geant4-threads   Geant4 thread-count sweep
  gun-noop         framework overhead: particle gun without Geant4
  fixed-target     fixed target on one thread, 20 and 420 events
  sweep            thread x parallelism sweep (CSV + plots)
  sweep-saturated  the same sweep with a fully loaded machine
EOF
  exit 2
}

[[ $# -eq 1 ]] || usage

case $1 in
pythia8)
  hf \
    --warmup 1 \
    --min-runs 5 \
    --reference "$standalone --events $EVENTS" \
    --reference-name 'standalone' \
    -n 'phlex noop' "$(phlex_cmd pythia8_noop "events=$EVENTS")" \
    -n 'phlex mc_only' "$(phlex_cmd pythia8_only "events=$EVENTS")" \
    --export-markdown bench-pythia8.md \
    --export-json bench-pythia8.json
  ;;
standalone)
  hf \
    --warmup 1 \
    --min-runs 5 \
    -n 'serial' "$standalone --events $EVENTS" \
    -n 'parallel 4T' "$standalone --events $EVENTS --threads 4" \
    -n 'parallel 8T' "$standalone --events $EVENTS --threads 8" \
    --export-markdown bench-standalone.md
  ;;
phlex-io)
  hf \
    --warmup 1 \
    --min-runs 5 \
    --reference "$(phlex_cmd pythia8_noop "events=$EVENTS")" \
    --reference-name 'phlex noop' \
    -n 'phlex mc_only' "$(phlex_cmd pythia8_only "events=$EVENTS")" \
    --export-markdown bench-phlex-io.md
  ;;
fairship)
  hf \
    --warmup 1 \
    --min-runs 5 \
    -n 'default' "$standalone --events $EVENTS" \
    -n 'fairship' "$standalone --events $EVENTS --fairship" \
    --export-markdown bench-fairship.md
  ;;
scaling)
  hf \
    --warmup 1 \
    --min-runs 3 \
    -L events 100,1000,10000,100000 \
    -n 'standalone {events}' "$standalone --events {events}" \
    --export-markdown bench-scaling-standalone.md
  hf \
    --warmup 1 \
    --min-runs 3 \
    -L events 100,1000,10000,100000 \
    -n 'phlex noop {events}' "$(phlex_cmd pythia8_noop "events={events}")" \
    --export-markdown bench-scaling-phlex.md
  ;;
geant4)
  hf \
    --warmup 1 \
    --min-runs 3 \
    --reference "$(phlex_cmd gun_st_bench "events=$G4_EVENTS") -j 1" \
    --reference-name 'G4 single-threaded' \
    -n "G4 multi-threaded (${G4_CONCURRENCY}T)" \
    "$(phlex_cmd gun_mt_noop "events=$G4_EVENTS" "concurrency=$G4_CONCURRENCY")" \
    --export-markdown bench-geant4.md \
    --export-json bench-geant4.json
  ;;
geant4-io)
  hf \
    --warmup 1 \
    --min-runs 3 \
    --reference "$(phlex_cmd gun_st_bench "events=$G4_EVENTS") -j 1" \
    --reference-name 'G4 ST noop' \
    -n 'G4 ST full' "$(phlex_cmd gun_st_full "events=$G4_EVENTS") -j 1" \
    --export-markdown bench-geant4-io.md
  ;;
geant4-threads)
  hf \
    --warmup 1 \
    --min-runs 3 \
    -L concurrency 1,2,4,8 \
    -n 'G4 {concurrency}T' \
    "$(phlex_cmd gun_mt_noop "events=$G4_EVENTS" "concurrency={concurrency}")" \
    --export-markdown bench-geant4-threads.md \
    --export-json bench-geant4-threads.json
  ;;
gun-noop)
  hf \
    --warmup 1 \
    --min-runs 5 \
    --reference "$(phlex_cmd gun_noop "events=$EVENTS")" \
    --reference-name 'gun noop (no G4)' \
    -n 'gun + G4 ST noop' "$(phlex_cmd gun_st_bench "events=$G4_EVENTS") -j 1" \
    --export-markdown bench-gun-noop.md
  ;;
fixed-target)
  # Fixed-target production on one thread, at two event counts: the difference
  # divided by the extra events is the time per event without initialisation
  # (compare with FairShip's run_fixedTarget.py, see docs/benchmarks.md).
  # Results are keyed by the short HEAD SHA, suffixed with -dirty if tracked
  # files have uncommitted changes.
  tag="$(git rev-parse --short HEAD)$(git diff --quiet HEAD -- || echo -dirty)"
  mkdir -p results
  hf \
    --runs 3 \
    -L events 20,420 \
    -n 'fixed target, {events} events' \
    "$(phlex_cmd fixed_target_st_bench "events={events}") -j 1" \
    --export-markdown "results/fixed_target_${tag}.md" \
    --export-json "results/fixed_target_${tag}.json"
  ;;
sweep | sweep-saturated)
  extra=()
  suffix=""
  if [[ $1 == sweep-saturated ]]; then
    extra=(--saturate)
    suffix=_saturated
  fi
  # The thread and parallelism lists are deliberately word-split.
  # shellcheck disable=SC2086
  python3 scripts/run_benchmark.py \
    --workflow "$BENCH_WORKFLOW" \
    --num-events "$BENCH_SWEEP_EVENTS" \
    --g4-threads $BENCH_G4_THREADS \
    --parallelism $BENCH_PARALLELISM \
    --repeats "$BENCH_REPEATS" \
    "${extra[@]}" \
    --output "benchmark_${BENCH_WORKFLOW}${suffix}.csv" \
    --plot
  ;;
*)
  usage
  ;;
esac
