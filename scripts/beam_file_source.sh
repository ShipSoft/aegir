#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# beam_file_source: write a small file in the layout of the BDSIM beam-line
# files (one TTree, one particle per entry), read it back with mc_only output
# (no Geant4) and check that
#   - every entry becomes one event holding one status-1 particle, placed at
#     the configured z with x/y converted from m to mm plus the offsets,
#   - the momentum is p times the direction cosines and the energy uses the
#     particle's mass (|p| for a PDG code TDatabasePDG does not know),
#   - the time is zero by default and the file's with keep_time,
#   - project_to_z moves each particle in a straight line to that z, adding
#     the flight time,
#   - the header carries the entry's weight (times weight_scale) and the entry
#     index as original_event_id,
#   - first_entry offsets the read and reading past the end fails,
#   - the tree is found without naming it, and a file with two trees, a
#     missing branch or a branch of the wrong type fails clearly,
#   - count_entries.py counts the tree.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat >"$workdir/read.jsonnet" <<'EOF'
function(infile, events, first_entry=0, keep_time=false, extra={}) {
  stage: 'simulation',
  driver: { cpp: 'generate_layers', layers: { event: { total: events } } },
  sources: {
    input: {
      cpp: 'beam_file_source',
      file: infile,
      z: 4444.0,
      x_offset: 1.5,
      y_offset: -2.5,
      first_entry: first_entry,
      keep_time: keep_time,
      weight_scale: 2.0,
    } + extra,
  },
  modules: {
    output: {
      cpp: 'sim_output_module',
      mode: 'mc_only',
      rntuple_file: std.extVar('simout'),
      histo_file: std.extVar('histo'),
    },
  },
}
EOF

# The fixture: a proton, a muon with a background-sized weight and an unknown
# PDG code. Same branch names and types as the facility files.
cat >"$workdir/make_input.py" <<'EOF'
import array
import math
import sys

import ROOT

path, layout = sys.argv[1], sys.argv[2]
rows = [
    # x [m], y [m], xp, yp, p [GeV], t [ns], pdgID, weight
    (0.0147, -0.0364, 1.4e-4, -3.6e-4, 399.4, 6.78e6, 2212, 1.0),
    (1.2032, -2.7931, -0.03266, 0.00601, 1.3434, 6.72e7, 13, 2.45e-11),
    (-0.5, 0.25, 0.01, -0.02, 7.0, 1.0e8, 9999999, 3.0e-12),
]
f = ROOT.TFile(path, "RECREATE")
names = ["HADRON_STOPPER", "OTHER"] if layout == "two_trees" else ["HADRON_STOPPER"]
for name in names:
    t = ROOT.TTree(name, name)
    bufs = {b: array.array("d", [0.0]) for b in ["x", "y", "xp", "yp", "zp", "p", "t", "weight"]}
    pdg = array.array("f" if layout == "bad_type" else "i", [0])
    for b, buf in bufs.items():
        if layout == "no_weight" and b == "weight":
            continue
        t.Branch(b, buf, f"{b}/D")
    t.Branch("pdgID", pdg, "pdgID/F" if layout == "bad_type" else "pdgID/I")
    for x, y, xp, yp, p, time, code, w in rows:
        for b, v in zip(["x", "y", "xp", "yp", "p", "t", "weight"], [x, y, xp, yp, p, time, w]):
            bufs[b][0] = v
        bufs["zp"][0] = math.sqrt(1.0 - xp * xp - yp * yp)
        pdg[0] = code
        t.Fill()
    t.Write()
ROOT.TNamed("description", "aegir test fixture").Write()
f.Close()
EOF

cat >"$workdir/check.py" <<'EOF'
import math
import os
import sys

import ROOT

out, first, keep_time = sys.argv[1], int(sys.argv[2]), sys.argv[3] == "true"
project = float(sys.argv[4]) if len(sys.argv) > 4 else None
rows = [
    (0.0147, -0.0364, 1.4e-4, -3.6e-4, 399.4, 6.78e6, 2212, 1.0, 0.93827),
    (1.2032, -2.7931, -0.03266, 0.00601, 1.3434, 6.72e7, 13, 2.45e-11, 0.10566),
    (-0.5, 0.25, 0.01, -0.02, 7.0, 1.0e8, 9999999, 3.0e-12, 0.0),
]
alive = []
reader = ROOT.RNTupleReader.Open("events", out)
particles = reader.GetView["std::vector<SHiP::MCParticle>"]("mc_particles")
headers = reader.GetView["SHiP::EventHeader"]("event_header")
alive.extend((reader, particles, headers))

errors = []
seen = []


def close(a, b, rel=1e-6):
    return math.isclose(a, b, rel_tol=rel, abs_tol=1e-9)


for i in range(reader.GetNEntries()):
    h = headers(i)
    entry = int(h.original_event_id)
    seen.append(entry)
    x, y, xp, yp, p, t, code, w, mass = rows[entry]
    ps = particles(i)
    if ps.size() != 1:
        errors.append(f"entry {entry}: {ps.size()} particles, expected 1")
        continue
    mc = ps[0]
    zp = math.sqrt(1.0 - xp * xp - yp * yp)
    z, vx, vy, vt = 4444.0, x * 1000 + 1.5, y * 1000 - 2.5, t
    if project is not None:
        path = (project - z) / zp  # mm
        vx, vy, z = vx + xp * path, vy + yp * path, project
        vt += path / (p / math.hypot(p, mass) * 299.792458)
    checks = {
        "pdg": (mc.pdg_code, code),
        "status": (mc.status, 1),
        "mother_id": (mc.mother_id, -1),
        "x": (mc.vertex[0], vx),
        "y": (mc.vertex[1], vy),
        "z": (mc.vertex[2], z),
        "px": (mc.momentum[0], p * xp),
        "py": (mc.momentum[1], p * yp),
        "pz": (mc.momentum[2], p * zp),
        "energy": (mc.energy, math.hypot(p, mass)),
        "time": (mc.time, vt if keep_time else 0.0),
        "weight": (h.weight, 2.0 * w),
    }
    for what, (got, want) in checks.items():
        # Masses come from the PDG table, which may differ in the last digits.
        rel = 1e-4 if what in ("energy", "time") else 1e-6
        if not close(float(got), float(want), rel):
            errors.append(f"entry {entry}: {what} = {got}, expected {want}")

if sorted(seen) != list(range(first, len(rows))):
    errors.append(f"entries read {sorted(seen)}, expected {first}..{len(rows) - 1}")
for e in errors:
    print(e)
sys.stdout.flush()
os._exit(1 if errors else 0)
EOF

run() {  # run <input> <events> <first_entry> <keep_time> [extra-config]
  local extra=${5:-"{}"}
  rm -f "$workdir/out.root"
  phlex -c <(jsonnet \
    --ext-str simout="$workdir/out.root" --ext-str histo="$workdir/histo.root" \
    --tla-str infile="$1" --tla-code events="$2" --tla-code first_entry="$3" \
    --tla-code keep_time="$4" --tla-code extra="$extra" \
    "$workdir/read.jsonnet")
}

expect_failure() {  # expect_failure <message> <run args...>
  local message=$1 out
  shift
  if out=$(run "$@" 2>&1); then
    echo "expected the job to fail with: $message"
    exit 1
  fi
  grep -qF "$message" <<<"$out" || {
    echo "unexpected error, wanted: $message"
    echo "$out"
    exit 1
  }
}

for layout in good two_trees no_weight bad_type; do
  python3 "$workdir/make_input.py" "$workdir/$layout.root" "$layout"
done

# count_entries.py finds the single tree.
count=$(python3 "$here/count_entries.py" "$workdir/good.root")
[ "$count" = 3 ] || { echo "count_entries: expected 3, got $count"; exit 1; }

# All entries, default time; then from an offset with the file's time.
run "$workdir/good.root" 3 0 false
python3 "$workdir/check.py" "$workdir/out.root" 0 false
run "$workdir/good.root" 2 1 true
python3 "$workdir/check.py" "$workdir/out.root" 1 true

# Moved in straight lines to z = 5000 mm, with the flight time added.
run "$workdir/good.root" 3 0 true "{project_to_z: 5000.0}"
python3 "$workdir/check.py" "$workdir/out.root" 0 true 5000

# Naming the tree works too.
run "$workdir/two_trees.root" 3 0 false "{tree: 'HADRON_STOPPER'}"
python3 "$workdir/check.py" "$workdir/out.root" 0 false

expect_failure "input exhausted" "$workdir/good.root" 3 1 false
expect_failure "holds 2 TTrees (HADRON_STOPPER, OTHER)" "$workdir/two_trees.root" 1 0 false
expect_failure "branch 'weight' missing" "$workdir/no_weight.root" 1 0 false
expect_failure "branch 'pdgID' in '$workdir/bad_type.root' does not have the expected type" \
  "$workdir/bad_type.root" 1 0 false
expect_failure "no TTree 'NOPE'" "$workdir/good.root" 1 0 false "{tree: 'NOPE'}"

echo "beam_file_source passed: conversion, header, offsets, tree lookup and errors"
