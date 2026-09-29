#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Generator status: the Geant4 module must build primaries only from entries
# the generator left undecayed (HepMC status 1), so that already-decayed
# mothers and beam particles are not tracked alongside their own daughters.
#
# A Pythia8 run with `record: 'full'` supplies a genuine mixed-status file;
# replaying it through file_source + Geant4 checks that
#   - exactly the status-1 entries become primaries, the rest being counted in
#     the aggregated skip line,
#   - `track_all_primaries` restores the old behaviour, which tracks the
#     decayed and beam entries too.
# The fixture itself is checked against the data model's mother invariants, so
# the record's provenance chain is verified along the way.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

n=3          # events; a full 400 GeV record is ~100 entries each
seed=12345   # fixed, so the record and every count below are reproducible

# Generate the full Pythia record (statuses and mother links intact) and write
# it as mc_particles, without simulating: this is only the fixture.
cat >"$workdir/write_full.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    pythia8: lib.pythia8 {
      record: std.extVar('record'),
      seed: std.parseInt(std.extVar('seed')),
    },
  },
  modules: {
    output: lib.mc_only_output(std.extVar('outfile'), std.extVar('histofile')),
  },
}
EOF

# Replay a stored mc_particles file through Geant4, with the status filter
# either on (the default) or off.
cat >"$workdir/replay.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    input: {
      cpp: 'file_source',
      input_file: std.extVar('infile'),
      product: 'mc_particles',
      skip: 0,
    },
  },
  modules: {
    geant4: lib.geant4 {
      track_all_primaries: std.extVar('trackall') == 'true',
    },
    output: lib.full_output(std.extVar('simout'), std.extVar('histo')),
  },
}
EOF

# Report, per event, how many stored MCParticles fall in each HepMC status
# class and how many Geant4 primaries the simulation produced. Primaries are
# the SimParticles whose parentId is 0 (G4Track::GetParentID() is 0 exactly for
# primaries), which is order-independent — the parallel writer emits entries in
# completion order. os._exit avoids a spurious PyROOT teardown crash, and every
# reader/view is kept alive until then.
cat >"$workdir/counts.py" <<'EOF'
import os
import sys

import ROOT

ROOT.gInterpreter.Declare('#include "SHiP/MCParticle.hpp"')

path = sys.argv[1]
alive = []

reader = ROOT.RNTupleReader.Open("events", path)
mc = reader.GetView["std::vector<SHiP::MCParticle>"]("mc_particles")
alive.extend((reader, mc))
sim = None
if "sim_particles" in [f.GetFieldName() for f in reader.GetDescriptor().GetTopLevelFields()]:
    sim = reader.GetView["std::vector<SHiP::SimParticle>"]("sim_particles")
    alive.append(sim)

for i in range(reader.GetNEntries()):
    parts = mc(i)
    total = len(parts)
    final = sum(1 for p in parts if p.status == 1)
    # Entries Geant4 would build a primary from with the filter disabled:
    # it silently refuses to track the short-lived ones (partons, strings).
    trackable = sum(1 for p in parts if p.status in (1, 2, 4))
    mothers = sum(1 for p in parts if p.motherId >= 0)
    # The data model's own invariant checks: indices in range, no -1 inside
    # the list, mothers.front() == motherId, elements distinct and never
    # self-referential. "Populated" additionally requires that an entry with a
    # mother carries the whole list, which only current-schema data can do.
    bad = 0 if ROOT.SHiP.mothersAreConsistent(parts) else 1
    if not ROOT.SHiP.mothersArePopulated(parts):
        bad += 1
    multi = sum(1 for p in parts if len(p.mothers) > 1)
    primaries = sum(1 for p in sim(i) if p.parentId == 0) if sim else -1
    print(f"{total} {final} {trackable} {mothers} {bad} {primaries} {multi}")

sys.stdout.flush()
os._exit(0)
EOF

# 1. Write the mixed-status fixture.
phlex -c <(jsonnet -J "$workflows" \
  --ext-str events="$n" \
  --ext-str seed="$seed" \
  --ext-str record=full \
  --ext-str outfile="$workdir/full.root" \
  --ext-str histofile="$workdir/full_hist.root" \
  "$workdir/write_full.jsonnet")

# 2. It must genuinely be mixed, or the rest of the test proves nothing: every
# event needs non-final-state entries, and the mother links must satisfy the
# data model's invariants, with at least one real mother and at least one
# entry carrying several — the case a single motherId could not represent.
python3 "$workdir/counts.py" "$workdir/full.root" >"$workdir/fixture.txt"
while read -r total final trackable mothers bad _ multi; do
  [ "$bad" = 0 ] || { echo "fixture: mother invariants violated"; exit 1; }
  [ "$mothers" -gt 0 ] || { echo "fixture: no entry has a mother"; exit 1; }
  [ "$multi" -gt 0 ] || {
    echo "fixture: no entry has several mothers — the full record should"
    echo "contain string-fragmentation hadrons with many parents"
    exit 1
  }
  [ "$final" -lt "$total" ] || {
    echo "fixture: all $total entries are final state — record is not mixed"
    exit 1
  }
  [ "$trackable" -gt "$final" ] || {
    echo "fixture: no decayed or beam entries to be filtered out"
    exit 1
  }
done <"$workdir/fixture.txt"

# 2b. The same record in final_state mode. Every mother of a final-state
# particle is itself dropped, so this is what exercises the "drop, do not
# record -1" half of the remap: pushing the sentinel into the list instead
# would fail mothersAreConsistent, which rejects any index outside [0, N).
phlex -c <(jsonnet -J "$workflows" \
  --ext-str events="$n" \
  --ext-str seed="$seed" \
  --ext-str record=final_state \
  --ext-str outfile="$workdir/fs.root" \
  --ext-str histofile="$workdir/fs_hist.root" \
  "$workdir/write_full.jsonnet")

python3 "$workdir/counts.py" "$workdir/fs.root" >"$workdir/fs.txt"
while read -r total final _ mothers bad _ _; do
  [ "$bad" = 0 ] || {
    echo "final_state: mother invariants violated — a dropped mother was"
    echo "probably recorded as -1 instead of being left out"
    exit 1
  }
  [ "$mothers" = 0 ] || {
    echo "final_state: $mothers entries kept a mother, but no mother of a"
    echo "final-state particle survives the filter"
    exit 1
  }
  [ "$final" = "$total" ] || {
    echo "final_state: $((total - final)) of $total entries are not final state"
    exit 1
  }
done <"$workdir/fs.txt"

# 3. Replay with the filter on (the default).
phlex -c <(jsonnet -J "$workflows" \
  --ext-str events="$n" \
  --ext-str infile="$workdir/full.root" \
  --ext-str trackall=false \
  --ext-str simout="$workdir/sim_filtered.root" \
  --ext-str histo="$workdir/valid_filtered.root" \
  "$workdir/replay.jsonnet") 2>&1 | tee "$workdir/filtered.log"

# Exactly the status-1 entries became primaries. The two other skip reasons are
# read back out of the aggregated warning rather than assumed to be zero.
python3 "$workdir/counts.py" "$workdir/sim_filtered.root" >"$workdir/filtered.txt"
while read -r total final _ _ _ primaries _; do
  # Skips for this event, matched by its mc_particles count.
  line=$(grep -F "of $total primaries" "$workdir/filtered.log" || true)
  lost=0
  if [ -n "$line" ]; then
    unknown=$(echo "$line" | sed -n 's/.*(\([0-9]*\) unknown PDG.*/\1/p')
    nomom=$(echo "$line" | sed -n 's/.*, \([0-9]*\) non-positive momentum.*/\1/p')
    notfinal=$(echo "$line" | sed -n 's/.*, \([0-9]*\) not final state.*/\1/p')
    lost=$((unknown + nomom))
    [ "$notfinal" = "$((total - final))" ] || {
      echo "filtered: log says $notfinal not final state, expected $((total - final))"
      exit 1
    }
  fi
  [ "$primaries" = "$((final - lost))" ] || {
    echo "filtered: tracked $primaries primaries, expected $((final - lost))"
    exit 1
  }
done <"$workdir/filtered.txt"

# 4. Replay the same file with the filter off. More primaries are tracked —
# the decayed and beam entries now become primaries alongside the daughters
# they already produced — and nothing is skipped for being non-final-state.
phlex -c <(jsonnet -J "$workflows" \
  --ext-str events="$n" \
  --ext-str infile="$workdir/full.root" \
  --ext-str trackall=true \
  --ext-str simout="$workdir/sim_all.root" \
  --ext-str histo="$workdir/valid_all.root" \
  "$workdir/replay.jsonnet") 2>&1 | tee "$workdir/all.log"

# Every skip line, if any, must report zero status-based skips. grep against a
# file rather than a pipe: under `set -o pipefail` an early-exiting grep in a
# pipeline kills the producer with SIGPIPE and fails the script.
if grep -F "not final state" "$workdir/all.log" >"$workdir/all_skips.txt"; then
  if grep -qv ", 0 not final state" "$workdir/all_skips.txt"; then
    echo "track_all_primaries: entries were still skipped for their status"
    exit 1
  fi
fi

# Bracketed rather than exact: Geant4 itself declines to track the short-lived
# entries (partons, strings, diquarks), so the count lands between "status 1
# only" and "every entry Geant4 has a non-short-lived definition for".
python3 "$workdir/counts.py" "$workdir/sim_all.root" >"$workdir/all.txt"
while read -r _ final trackable _ _ primaries _; do
  [ "$primaries" -gt "$final" ] || {
    echo "track_all_primaries: tracked $primaries, no more than the $final"
    echo "final-state entries — the filter made no difference"
    exit 1
  }
  [ "$primaries" -le "$trackable" ] || {
    echo "track_all_primaries: tracked $primaries, more than the $trackable"
    echo "final-state, decayed and beam entries available"
    exit 1
  }
done <"$workdir/all.txt"

echo "generator status check passed: only HepMC status-1 primaries are tracked;"
echo "track_all_primaries restores the decayed and beam entries"
