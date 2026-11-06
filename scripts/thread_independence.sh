#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Thread-independence check for the Pythia-based generators (issue #133):
# with a fixed seed, a job must write the same events whatever the number of
# threads. Each event's random numbers are keyed to its event number, so
# the order phlex happens to process events in must not matter.
#
# - fixed_target, generator output only: its target choice and vertex are
#   keyed to the event number, and must stay paired with the Pythia event.
# - pythia8 (serial) through Geant4: the generator output alone cannot show
#   which event number an event got, but Geant4 seeds each event from its
#   event number, so a mismatch changes the simulated result.
#
# Before the fix, out-of-order processing showed up in roughly one run in
# four at -j 8, so a pass here is strong evidence rather than proof.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat >"$workdir/job.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
local generator = std.extVar('generator');
local simulate = generator == 'pythia8';
{
  stage: lib.stage,
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    gen: lib[generator] { seed: 20261005 },
  } + (if simulate then {
         field: lib.null_field,
         geometry: lib.builtin_geometry,
       } else {}),
  modules: {
    output: (if simulate then lib.full_output else lib.mc_only_output)(
      std.extVar('outfile'), std.extVar('outfile') + '.histo.root'
    ),
  } + (if simulate then { geant4: lib.geant4 { seed: 20261005 } } else {}),
}
EOF

run() {  # generator events threads outfile
  phlex -j "$3" -c <(jsonnet -J "$workflows" \
    --ext-str generator="$1" \
    --ext-str events="$2" \
    --ext-str outfile="$4" \
    "$workdir/job.jsonnet") >"$4.log" 2>&1 || {
    echo "thread-independence check FAILED: $1 at -j $3 did not run" >&2
    tail -20 "$4.log" >&2
    exit 1
  }
}

check() {  # generator events
  run "$1" "$2" 1 "$workdir/$1-j1.root"
  run "$1" "$2" 8 "$workdir/$1-j8.root"
  if ! python3 "$here/compare_rntuples.py" \
    "$workdir/$1-j1.root" "$workdir/$1-j8.root"; then
    echo "thread-independence check FAILED: $1 output depends on the thread count" >&2
    exit 1
  fi
}

check fixed_target 100
check pythia8 10

echo "thread-independence check passed: same events at -j 1 and -j 8"
