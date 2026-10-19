#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Event header round-trip: file_source must publish the EventHeader stored in
# its input rather than the default-constructed one, and must still replay a
# file written before the event_header field existed. Two checks:
#   - a file whose headers are all distinguishable from the unweighted default
#     reads back with those headers intact,
#   - a file with no event_header field at all reads back with every event
#     carrying the default (weight 1.0, original_event_id -1).
# This reads the input back through the mc_only output path, so it needs no
# Geant4, geometry or field and stays fast and deterministic.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

n=8  # events written to the input file

# Read the input back and re-write the MCParticles (mc_only), which preserves
# the event header the source published. The committed file_read.jsonnet runs
# Geant4, which this check does not need.
cat >"$workdir/read.jsonnet" <<'EOF'
local n_events = std.parseInt(std.extVar('events'));
{
  stage: 'simulation',
  driver: { cpp: 'generate_layers', layers: { event: { total: n_events } } },
  sources: {
    input: {
      cpp: 'file_source',
      stage: 'simulation',
      input_file: std.extVar('infile'),
      product: 'mc_particles',
      skip: 0,
    },
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

# Build a small MCParticle file whose event headers are all distinguishable from
# the unweighted default (1.0, -1). The particle gun publishes that default for
# every event, so a gun-written file cannot tell a header that was really read
# back from one that fell through to the default. With --no-header the
# event_header field is left out entirely, reproducing a file written before the
# field existed.
cat >"$workdir/make_header_input.py" <<'EOF'
import os
import sys

import ROOT

out, n = sys.argv[1], int(sys.argv[2])
with_header = "--no-header" not in sys.argv

model = ROOT.RNTupleModel.Create()
model.MakeField["std::vector<SHiP::MCParticle>"]("mc_particles")
if with_header:
    model.MakeField["SHiP::EventHeader"]("event_header")
writer = ROOT.RNTupleWriter.Recreate(ROOT.std.move(model), "events", out)
entry = writer.CreateEntry()

for i in range(n):
    particles = entry["mc_particles"]
    particles.clear()
    p = ROOT.SHiP.MCParticle()
    p.pdgCode = 13
    p.vertex[2] = -500.0
    p.momentum[2] = 10.0 + i
    p.energy = 10.0 + i
    p.motherId = -1
    p.status = 1
    particles.push_back(p)
    if with_header:
        header = entry["event_header"]
        header.weight = 2.0 + i  # never 1.0, the default
        header.original_event_id = 100 + i
    writer.Fill(entry)

del writer  # flushes and closes the file
os._exit(0)
EOF

# Compare the event headers a file_source run published against the input they
# came from, or (--default) assert they are all the unweighted default. The
# parallel writer emits in completion order, not event order, so compare the
# multiset of headers rather than positions.
cat >"$workdir/check_header.py" <<'EOF'
import os
import sys

import ROOT

# Keep every reader and view alive until os._exit; PyROOT teardown of a closed
# RNTupleReader can crash and mask the real exit status.
alive = []


def headers(path):
    reader = ROOT.RNTupleReader.Open("events", path)
    view = reader.GetView["SHiP::EventHeader"]("event_header")
    alive.extend((reader, view))
    return sorted(
        (int(view(i).original_event_id), float(view(i).weight))
        for i in range(reader.GetNEntries())
    )


got = headers(sys.argv[1])
if sys.argv[2] == "--default":
    want = [(-1, 1.0)] * len(got)
else:
    want = headers(sys.argv[2])
ok = bool(got) and got == want
if not ok:
    print(f"event_header mismatch: got {got}, expected {want}")
sys.stdout.flush()  # os._exit skips the buffer flush
os._exit(0 if ok else 1)
EOF

# 1. The event header survives a read-back: file_source must publish the header
#    stored in the input, not the unweighted default.
python3 "$workdir/make_header_input.py" "$workdir/header_input.root" "$n"
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str infile="$workdir/header_input.root" \
  --ext-str simout="$workdir/sim_header.root" \
  --ext-str histo="$workdir/valid_header.root" \
  "$workdir/read.jsonnet")
python3 "$workdir/check_header.py" "$workdir/sim_header.root" \
  "$workdir/header_input.root"

# 2. A file written before the event_header field existed still replays, and
#    publishes the unweighted default rather than failing to open the view.
python3 "$workdir/make_header_input.py" "$workdir/legacy_input.root" "$n" --no-header
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str infile="$workdir/legacy_input.root" \
  --ext-str simout="$workdir/sim_legacy.root" \
  --ext-str histo="$workdir/valid_legacy.root" \
  "$workdir/read.jsonnet")
got=$(python3 "$here/count_entries.py" "$workdir/sim_legacy.root")
[ "$got" = "$n" ] || { echo "legacy read: expected $n events, got $got"; exit 1; }
python3 "$workdir/check_header.py" "$workdir/sim_legacy.root" --default

echo "event header round-trip passed: the stored header is preserved, and a file"
echo "without the event_header field falls back to the unweighted default"
