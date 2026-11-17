#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Energy cut: with `energy_cut` on, secondaries created below the threshold
# must be dropped before they are tracked, as FairShip's PreTrack() does, and
# not after a first step. Dropped tracks are never recorded, so with every
# particle written (keep_particles 'all', no particle_ke_cut) the output shows
# it directly: no secondary may start below the threshold.
#
# Pions fired into the tungsten block of the builtin geometry shower there,
# which gives plenty of secondaries on both sides of the threshold. The
# primaries are exempt: they are always tracked and recorded.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

n=5
threshold=0.5  # GeV

cat >"$workdir/energy_cut.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  resources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
  },
  sources: {
    gun: lib.pencil_gun { pdg: 211, p_min: 50.0, p_max: 50.0, seed: 12345 },
  },
  modules: {
    geant4: lib.geant4 {
      energy_cut: true,
      energy_cut_threshold: std.parseJson(std.extVar('threshold')),
      seed: 12345,
    },
    output: lib.full_output(std.extVar('simout'), std.extVar('histo')),
  },
}
EOF

# Print the number of primaries, of secondaries, and of secondaries starting
# below the threshold. os._exit avoids a spurious PyROOT teardown crash, and
# the reader and view are kept alive until then.
cat >"$workdir/counts.py" <<'EOF'
import os
import sys

import ROOT

path, threshold = sys.argv[1], float(sys.argv[2])
reader = ROOT.RNTupleReader.Open("events", path)
sim = reader.GetView["std::vector<SHiP::SimParticle>"]("sim_particles")
alive = [reader, sim]

primaries = secondaries = below = 0
for i in range(reader.GetNEntries()):
    for p in sim(i):
        if p.parent_id == 0:
            primaries += 1
            continue
        secondaries += 1
        if p.energy < threshold:
            below += 1
print(primaries, secondaries, below)

sys.stdout.flush()
os._exit(0)
EOF

(cd "$workdir" && phlex -j 1 -c <(jsonnet -J "$workflows" \
  --ext-str events="$n" \
  --ext-str threshold="$threshold" \
  --ext-str simout="$workdir/sim.root" \
  --ext-str histo="$workdir/histo.root" \
  "$workdir/energy_cut.jsonnet"))

read -r primaries secondaries below < <(python3 "$workdir/counts.py" "$workdir/sim.root" "$threshold")
echo "primaries $primaries, secondaries $secondaries, of which $below start below $threshold GeV"

if [[ "$primaries" -ne "$n" ]]; then
  echo "FAIL: expected $n recorded primaries, found $primaries"
  exit 1
fi
# Without showers the check below would pass for the wrong reason.
if [[ "$secondaries" -lt "$n" ]]; then
  echo "FAIL: only $secondaries secondaries recorded, the pions did not shower"
  exit 1
fi
if [[ "$below" -ne 0 ]]; then
  echo "FAIL: $below secondaries below the energy cut were tracked"
  exit 1
fi

echo "energy cut passed"
