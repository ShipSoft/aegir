#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# file_source round-trip: write an RNTuple of MCParticles, then read it back
# through file_source + Geant4 and check that
#   - count_entries.py reports the written event count (the read-all helper),
#   - reading the whole file processes every event,
#   - the `skip` offset starts at the requested entry and processes exactly the
#     requested number of events, serving the correct entries,
#   - the event header is read back from the input, and falls back to the
#     unweighted default for a file written before that field existed.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

n=8       # events written to the input file
skip=3    # offset for the second read
nread=5   # events processed in the offset read (n - skip)

# A read workflow with a parameterised skip, so the offset can be exercised
# (the committed file_read.jsonnet fixes skip to 0). This reads the file back
# and re-writes the MCParticles (mc_only), exercising file_source without
# depending on Geant4/geometry/field, so the check is fast and deterministic.
cat >"$workdir/read.jsonnet" <<'EOF'
local n_events = std.parseInt(std.extVar('events'));
{
  driver: { cpp: 'generate_layers', layers: { event: { total: n_events } } },
  sources: {
    input: {
      cpp: 'file_source',
      input_file: std.extVar('infile'),
      product: 'mc_particles',
      skip: std.parseInt(std.extVar('skip')),
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

# Check the offset read served exactly the input entries [skip, skip+N). The
# parallel RNTuple writer emits entries in completion order, not event order, so
# compare the multiset of per-event particle signatures rather than positions.
# sim_output preserves the mc_particles it received, so the set of output events
# must equal the set of input events in that window. os._exit avoids a spurious
# PyROOT teardown crash masking the real exit status.
cat >"$workdir/check_offset.py" <<'EOF'
import os
import sys

import ROOT

out_path, in_path, skip = sys.argv[1], sys.argv[2], int(sys.argv[3])
vt = "std::vector<SHiP::MCParticle>"

# Keep every reader and view alive until os._exit: letting one be garbage
# collected mid-script triggers a PyROOT RNTuple teardown crash.
alive = []


def signatures(path, lo, hi):
    reader = ROOT.RNTupleReader.Open("events", path)
    view = reader.GetView[vt]("mc_particles")
    alive.extend((reader, view))
    events = []
    for i in range(lo, hi):
        events.append(
            tuple(
                (int(p.pdgCode), p.momentum[0], p.momentum[1], p.momentum[2])
                for p in view(i)
            )
        )
    return sorted(events)


out_reader = ROOT.RNTupleReader.Open("events", out_path)
alive.append(out_reader)
n = out_reader.GetNEntries()
out_events = signatures(out_path, 0, n)
in_events = signatures(in_path, skip, skip + n)
ok = out_events == in_events
if not ok:
    print("offset mismatch: output events != input window [skip, skip+n)")
os._exit(0 if ok else 1)
EOF

# 1. Write the input file (particle gun -> MC-only RNTuple).
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str outfile="$workdir/input.root" \
  --ext-str histofile="$workdir/input_hist.root" \
  "$workflows/file_write_mc.jsonnet")

# 2. The count helper reports the written event count (drives read-all).
count=$(python3 "$here/count_entries.py" "$workdir/input.root")
[ "$count" = "$n" ] || { echo "count_entries: expected $n, got $count"; exit 1; }

# 3. Read the whole file back (mc_only); every event is processed.
phlex -c <(jsonnet \
  --ext-str events="$count" \
  --ext-str infile="$workdir/input.root" \
  --ext-str skip=0 \
  --ext-str simout="$workdir/sim_all.root" \
  --ext-str histo="$workdir/valid_all.root" \
  "$workdir/read.jsonnet")
got=$(python3 "$here/count_entries.py" "$workdir/sim_all.root")
[ "$got" = "$n" ] || { echo "read-all: expected $n events, got $got"; exit 1; }

# 4. Read from an offset; exactly nread events, starting at entry `skip`.
phlex -c <(jsonnet \
  --ext-str events="$nread" \
  --ext-str infile="$workdir/input.root" \
  --ext-str skip="$skip" \
  --ext-str simout="$workdir/sim_skip.root" \
  --ext-str histo="$workdir/valid_skip.root" \
  "$workdir/read.jsonnet")
got=$(python3 "$here/count_entries.py" "$workdir/sim_skip.root")
[ "$got" = "$nread" ] || { echo "offset read: expected $nread events, got $got"; exit 1; }

# 5. The offset read served the correct entries.
python3 "$workdir/check_offset.py" "$workdir/sim_skip.root" "$workdir/input.root" "$skip"

# Assert a full-simulation RNTuple actually tracked the file's primaries, i.e.
# the sim_particles field is non-empty across events (os._exit dodges a PyROOT
# teardown crash). Used to check the Geant4 re-simulation stages below.
cat >"$workdir/check_nonempty.py" <<'EOF'
import os
import sys

import ROOT

path, field = sys.argv[1], sys.argv[2]
reader = ROOT.RNTupleReader.Open("events", path)
view = reader.GetView[f"std::vector<SHiP::{sys.argv[3]}>"](field)
total = sum(view(i).size() for i in range(reader.GetNEntries()))
if total == 0:
    print(f"{field}: empty across all events")
os._exit(0 if total > 0 else 1)
EOF

# 6. Re-simulate the MC file through Geant4 (the file-driven particle gun): every
#    event is processed and the primaries are tracked (non-empty sim_particles).
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str infile="$workdir/input.root" \
  --ext-str simout="$workdir/g4_mc.root" \
  --ext-str histo="$workdir/g4_mc_valid.root" \
  "$workflows/file_read.jsonnet")
got=$(python3 "$here/count_entries.py" "$workdir/g4_mc.root")
[ "$got" = "$n" ] || { echo "geant4 mc read: expected $n events, got $got"; exit 1; }
python3 "$workdir/check_nonempty.py" "$workdir/g4_mc.root" sim_particles SimParticle

# 7. SimParticle path: produce a sim_particles file (gun -> Geant4 -> full), then
#    read it back with the SimParticle->MCParticle projection and re-simulate.
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str outfile="$workdir/sim_input.root" \
  --ext-str histofile="$workdir/sim_input_hist.root" \
  "$workflows/file_write_sim.jsonnet")
sn=$(python3 "$here/count_entries.py" "$workdir/sim_input.root")
[ "$sn" = "$n" ] || { echo "sim write: expected $n events, got $sn"; exit 1; }
python3 "$workdir/check_nonempty.py" "$workdir/sim_input.root" sim_particles SimParticle

phlex -c <(jsonnet \
  --ext-str events="$sn" \
  --ext-str infile="$workdir/sim_input.root" \
  --ext-str simout="$workdir/g4_sim.root" \
  --ext-str histo="$workdir/g4_sim_valid.root" \
  "$workflows/file_read_sim.jsonnet")
got=$(python3 "$here/count_entries.py" "$workdir/g4_sim.root")
[ "$got" = "$sn" ] || { echo "geant4 sim read: expected $sn events, got $got"; exit 1; }
python3 "$workdir/check_nonempty.py" "$workdir/g4_sim.root" sim_particles SimParticle

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
# came from, or (--default) assert they are all the unweighted default. Same
# multiset comparison as check_offset.py: the parallel writer emits in
# completion order, not event order.
cat >"$workdir/check_header.py" <<'EOF'
import os
import sys

import ROOT

# Keep every reader and view alive until os._exit, as in check_offset.py.
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

# 8. The event header survives a read-back: file_source must publish the header
#    stored in the input, not the unweighted default.
python3 "$workdir/make_header_input.py" "$workdir/header_input.root" "$n"
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str infile="$workdir/header_input.root" \
  --ext-str skip=0 \
  --ext-str simout="$workdir/sim_header.root" \
  --ext-str histo="$workdir/valid_header.root" \
  "$workdir/read.jsonnet")
python3 "$workdir/check_header.py" "$workdir/sim_header.root" \
  "$workdir/header_input.root"

# 9. A file written before the event_header field existed still replays, and
#    publishes the unweighted default rather than failing to open the view.
python3 "$workdir/make_header_input.py" "$workdir/legacy_input.root" "$n" --no-header
phlex -c <(jsonnet \
  --ext-str events="$n" \
  --ext-str infile="$workdir/legacy_input.root" \
  --ext-str skip=0 \
  --ext-str simout="$workdir/sim_legacy.root" \
  --ext-str histo="$workdir/valid_legacy.root" \
  "$workdir/read.jsonnet")
got=$(python3 "$here/count_entries.py" "$workdir/sim_legacy.root")
[ "$got" = "$n" ] || { echo "legacy read: expected $n events, got $got"; exit 1; }
python3 "$workdir/check_header.py" "$workdir/sim_legacy.root" --default

echo "file_source round-trip passed: count helper, read-all, skip offset,"
echo "Geant4 re-simulation (MCParticle and SimParticle inputs), and the event"
echo "header (preserved when stored, default when the field is absent)"
