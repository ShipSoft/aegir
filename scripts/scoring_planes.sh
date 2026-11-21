#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Scoring planes: run gun_st_scoring_planes with neutrinos and with muons and
# check that every plane records exactly the tracks the geometry says cross
# it, at its entry face, with the scoring-plane detector id; that neutrinos
# are recorded although they deposit nothing; that a plane cutting through the
# target records like any other (the planes live in a parallel world); and
# that the killing plane stops what it records. Check that keep_particles:
# 'hit_ancestry' writes exactly the hit tracks and their ancestors. Check that
# the recording plane's plane_particles hold the tracks it recorded, as
# they entered the plane, with their full ancestry, and that a replay of them
# starts exactly those tracks again, from the plane. Then check that bad
# configurations fail with a clear error instead of running, and that the
# fixed-target production workflow evaluates to the intended settings.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflow="$here/../workflows/gun_st_scoring_planes.jsonnet"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat >"$workdir/check_planes.py" <<'EOF'
import math
import os
import sys

import ROOT

path, pdg = sys.argv[1], int(sys.argv[2])
SCORING_PLANE = 100  # SHiP::aegir::g4::kScoringPlaneDetectorId
TOL = 1e-3  # mm
# Plane 1: upstream face 10 mm behind the target's end at z = 500 mm.
# Plane 2: tube r = 80-81 mm, |z| <= 500 mm. Plane 3: upstream face at 2999 mm.
# Plane 4: |x|, |y| <= 40 mm, upstream face at z = 249 mm, inside the tungsten.
# Plane 3 kills every track reaching it but records only those above KE3.
Z1, R2, Z3 = 510.0, 80.0, 2999.0
KE3 = 50.0  # GeV
Z4, HALF4 = 249.0, 40.0
theta_tube = math.atan2(R2, 500.0)  # below: leaves through the tube's end

reader = ROOT.RNTupleReader.Open("events", path)
hits_view = reader.GetView["std::vector<SHiP::SimHit>"]("sim_hits")
parts_view = reader.GetView["std::vector<SHiP::SimParticle>"]("sim_particles")
errors = []
counts = {1: 0, 2: 0, 3: 0, 4: 0}

n = reader.GetNEntries()
for i in range(n):
    primary = next(p for p in parts_view(i) if p.track_id == 1)
    px, py, pz = primary.momentum
    theta = math.atan2(math.hypot(px, py), pz)
    planes = {1: [], 2: [], 3: [], 4: []}
    detector = []
    for h in hits_view(i):
        if h.detector_id != SCORING_PLANE:
            detector.append(h)
            continue
        if h.geometry_node_id not in planes or h.energy_deposit != 0:
            errors.append(f"event {i}: bad plane hit id={h.geometry_node_id}")
            continue
        counts[h.geometry_node_id] += 1
        if h.track_id == 1:
            planes[h.geometry_node_id].append(h)

    def expect(cond, what):
        if not cond:
            errors.append(f"event {i} (theta={theta:.4f}): {what}")

    # Every primary crosses plane 1 and is killed on plane 3, whether or not
    # plane 3 records it.
    expect(len(planes[1]) == 1, f"{len(planes[1])} hits on plane 1")
    m = 0.0 if pdg in (12, 14, 16) else 0.1056584  # GeV

    def kinetic(h):
        p = math.sqrt(sum(c * c for c in h.momentum))
        return math.sqrt(p * p + m * m) - m

    # Whether plane 3 records the primary depends on its kinetic energy there.
    # Between plane 1 and plane 3 there is only air and 0.6 mm of silicon, so
    # the energy recorded on plane 1 predicts it; the tungsten before plane 1
    # can take any amount of a muon's energy (radiative losses).
    if planes[1]:
        ke1 = kinetic(planes[1][0])
        if abs(ke1 - KE3) > 1.0:
            want3 = 1 if ke1 > KE3 else 0
            expect(
                len(planes[3]) == want3,
                f"{len(planes[3])} hits on plane 3 ({ke1:.1f} GeV at plane 1)",
            )
    for h in planes[1]:
        expect(abs(h.position[2] - Z1) < TOL, f"plane 1 hit at z={h.position[2]}")
    for h in planes[3]:
        expect(abs(h.position[2] - Z3) < TOL, f"plane 3 hit at z={h.position[2]}")
        ke3 = kinetic(h)
        expect(ke3 >= KE3 - 1e-6, f"plane 3 recorded a track at {ke3:.2f} GeV")
    end_z = primary.endpoint[2]
    expect(Z3 - TOL < end_z < Z3 + 2.0 + TOL, f"primary ends at z={end_z}")

    if pdg in (12, 14, 16):
        # Straight lines: the tube is crossed exactly when the cone angle
        # takes the neutrino through its wall, entering at r = 80 mm.
        expect(not detector, f"{len(detector)} silicon hits from a neutrino")
        if abs(theta - theta_tube) > 1e-3:
            want = 1 if theta > theta_tube else 0
            expect(len(planes[2]) == want, f"{len(planes[2])} tube hits")
        for h in planes[2]:
            r = math.hypot(h.position[0], h.position[1])
            expect(abs(r - R2) < TOL, f"tube hit at r={r}")
        # Plane 4 is crossed when the straight line is inside its square at
        # its upstream face; skip lines grazing an edge.
        x4, y4 = Z4 * px / pz, Z4 * py / pz
        edge = HALF4 - max(abs(x4), abs(y4))
        if abs(edge) > 0.5:
            want = 1 if edge > 0 else 0
            expect(len(planes[4]) == want, f"{len(planes[4])} hits on plane 4")
        for h in planes[4]:
            expect(abs(h.position[2] - Z4) < TOL, f"plane 4 hit at z={h.position[2]}")
    else:
        # Charged: the silicon at z = 2 m still records it, the killing
        # plane at 3 m stops it before any later silicon plane.
        z_si = [h.position[2] for h in detector if h.track_id == 1]
        expect(any(abs(z - 2000.0) < 1.0 for z in z_si), "no silicon hit at 2 m")
        expect(all(z < Z3 for z in z_si), f"silicon hit behind the kill plane {z_si}")

for e in errors[:20]:
    print(e)
print(f"{n} events, plane hits {counts}, {len(errors)} problem(s)")
ok = n > 0 and not errors and all(counts[k] > 0 for k in (1, 2, 3, 4))
sys.stdout.flush()
os._exit(0 if ok else 1)  # dodge a PyROOT RNTuple teardown crash
EOF

# plane_particles: one status-1 entry per track plane 3 recorded going
# forward, in crossing order, at its hit on the plane. Its mother is the
# same track at its origin (as in sim_particles, when it is there); everything else is ancestry
# with another status, and every chain of mothers ends at an entry without
# any.
cat >"$workdir/check_plane_particles.py" <<'EOF'
import math
import os
import sys

import ROOT

# Every recorded track and all of its ancestors must be in sim_particles too,
# whatever the particle_ke_cut. With --want-history KE_CUT, some ancestry must
# be written, and some of those tracks must be below KE_CUT (GeV), which
# sim_particles would otherwise leave out.
path, want_history = sys.argv[1], sys.argv[2] == "--want-history"
ke_cut = float(sys.argv[3]) if want_history else 0.0
SCORING_PLANE, RECORDING = 100, 3

reader = ROOT.RNTupleReader.Open("events", path)
hits_view = reader.GetView["std::vector<SHiP::SimHit>"]("sim_hits")
parts_view = reader.GetView["std::vector<SHiP::SimParticle>"]("sim_particles")
plane_view = reader.GetView["std::vector<SHiP::MCParticle>"]("plane_particles")
errors = []
recorded_total = history_total = below_cut = 0

n = reader.GetNEntries()
for i in range(n):
    def expect(cond, what):
        if not cond:
            errors.append(f"entry {i}: {what}")

    hits = {}  # first forward hit of each recorded track, in crossing order
    for h in hits_view(i):
        if (h.detector_id == SCORING_PLANE and h.geometry_node_id == RECORDING
                and h.momentum[2] > 0 and h.track_id not in hits):
            hits[h.track_id] = (tuple(h.position), tuple(h.momentum), h.time)
    tracks = {p.track_id: p for p in parts_view(i)}
    recorded = list(hits)
    written = {(p.pdg_code, tuple(p.vertex)) for p in tracks.values()}
    out = list(plane_view(i))
    finals = [p for p in out if p.status == 1]
    expect(len(finals) == len(recorded),
           f"{len(finals)} status-1 entries for {len(recorded)} recorded tracks")
    expect(all(p.status == 1 for p in out[: len(finals)]),
           "recorded tracks do not come first")
    def close(a, b):
        # A recorded primary is the generator's particle, which differs from
        # Geant4's copy by the round trip through Geant4 units.
        return all(math.isclose(x, y, rel_tol=1e-9, abs_tol=1e-9)
                   for x, y in zip(a, b))

    for track_id, p in zip(recorded, out):
        sp = tracks.get(track_id)
        expect(sp is not None, f"recorded track {track_id} not in sim_particles")
        chain = track_id
        while chain in tracks and tracks[chain].parent_id != 0:
            if tracks[chain].energy < ke_cut:
                below_cut += 1
            chain = tracks[chain].parent_id
        expect(chain in tracks, f"track {track_id}: ancestor {chain} not written")
        position, momentum, time = hits[track_id]
        expect(close(p.vertex, position) and close(p.momentum, momentum)
               and close([p.time], [time]),
               f"track {track_id} is not at its hit on the plane")
        origin = out[p.mother_id] if p.mother_id >= 0 else None
        expect(origin is not None and origin.status != 1,
               f"track {track_id}: no history entry for its origin")
        if sp is not None and origin is not None:
            expect(origin.pdg_code == sp.pdg_code == p.pdg_code
                   and close(origin.vertex, sp.vertex)
                   and close(origin.momentum, sp.momentum),
                   f"track {track_id}: its mother is not the track at its origin")
        pmag = math.sqrt(sum(c * c for c in p.momentum))
        expect(p.energy >= pmag * (1 - 1e-12), f"track {track_id}: E < |p|")
    size = len(out)
    # Every Geant4 track in the history is written; primaries (no mothers
    # here) are the generator's copy, so they are matched above instead.
    expect(all((p.pdg_code, tuple(p.vertex)) in written
               for p in out[len(finals):] if p.mother_id >= 0),
           "an ancestor is missing from sim_particles")
    for k, p in enumerate(out):
        mothers = list(p.mothers)
        expect(p.mother_id == (mothers[0] if mothers else -1), f"{k}: mother_id")
        expect(all(0 <= m < size and m != k for m in mothers)
               and len(set(mothers)) == len(mothers), f"{k}: bad mothers")
    for k in range(len(finals)):
        seen = set()
        while out[k].mother_id >= 0 and k not in seen:
            seen.add(k)
            k = out[k].mother_id
        expect(out[k].mother_id < 0, "a chain of mothers loops")
    recorded_total += len(recorded)
    history_total += size - len(finals)

for e in errors[:20]:
    print(e)
print(f"{n} entries, {recorded_total} recorded tracks, {history_total} "
      f"ancestors, {below_cut} written below the particle_ke_cut, "
      f"{len(errors)} problem(s)")
ok = n > 0 and recorded_total > 0 and not errors
if want_history and (history_total == 0 or below_cut == 0):
    print("no ancestry, or none of it below the particle_ke_cut")
    ok = False
sys.stdout.flush()
os._exit(0 if ok else 1)
EOF

for pdg in 14 13; do
  (cd "$workdir" && phlex -c <(jsonnet --tla-code pdg="$pdg" "$workflow"))
  python3 "$workdir/check_planes.py" "$workdir/gun_st_scoring_planes.root" "$pdg"
  python3 "$workdir/check_plane_particles.py" "$workdir/gun_st_scoring_planes.root" \
    --no-history
  mv "$workdir/gun_st_scoring_planes.root" "$workdir/all_$pdg.root"
  rm -f "$workdir"/gun_st_scoring_planes*.root
done

# keep_particles: 'hit_ancestry' on the same (seeded) muon run: the hits must
# be unchanged, and the particles exactly those of the full run that left a
# hit or are an ancestor of one.
cat >"$workdir/check_ancestry.py" <<'EOF'
import os
import sys

import ROOT

full = ROOT.RNTupleReader.Open("events", sys.argv[1])
pruned = ROOT.RNTupleReader.Open("events", sys.argv[2])

def by_event(r):
    # The writer stores events in the order they finish, not by number.
    number = r.GetView["std::uint64_t"]("event_number")
    hits = r.GetView["std::vector<SHiP::SimHit>"]("sim_hits")
    parts = r.GetView["std::vector<SHiP::SimParticle>"]("sim_particles")
    return {
        int(number(i)): (
            [h.track_id for h in hits(i)],
            [(p.track_id, p.parent_id) for p in parts(i)],
        )
        for i in range(r.GetNEntries())
    }


a, b = by_event(full), by_event(pruned)
errors = []
n = len(a)
if sorted(a) != sorted(b):
    errors.append("the two runs wrote different events")
written = total = 0
for i in sorted(a.keys() & b.keys()):
    (hits, parts_a), (hits_b, parts_b) = a[i], b[i]
    if hits != hits_b:
        errors.append(f"event {i}: hits differ")
    parent = dict(parts_a)
    want = set()
    for t in hits:
        while t in parent and t not in want:
            want.add(t)
            t = parent[t]
    got = [t for t, _ in parts_b]
    if sorted(got) != sorted(want):
        errors.append(f"event {i}: kept {len(got)} particles, expected {len(want)}")
    kept = set(got)
    for track, mother in parts_b:
        if mother != 0 and mother not in kept:
            errors.append(f"event {i}: track {track} has no parent {mother}")
    written += len(got)
    total += len(parts_a)

for e in errors[:20]:
    print(e)
print(f"{n} events, kept {written} of {total} particles, {len(errors)} problem(s)")
sys.stdout.flush()
os._exit(0 if n > 0 and written < total and not errors else 1)
EOF

cat >"$workdir/hit_ancestry.jsonnet" <<EOF
local wf = import '$workflow';
wf(pdg=13) { modules+: { geant4+: { keep_particles: 'hit_ancestry' } } }
EOF
(cd "$workdir" && phlex -c <(jsonnet "$workdir/hit_ancestry.jsonnet"))
python3 "$workdir/check_ancestry.py" "$workdir/all_13.root" \
  "$workdir/gun_st_scoring_planes.root"
rm -f "$workdir"/gun_st_scoring_planes*.root

# Pions showering in the tungsten, recorded on plane 3 from 1 GeV: the
# recorded tracks are mostly secondaries, so their ancestry runs through
# several Geant4 generations. With a particle_ke_cut above the plane's
# threshold, some recorded tracks and ancestors are below the cut, and must
# still be in both plane_particles and sim_particles.
cat >"$workdir/pions.jsonnet" <<EOF
local wf = import '$workflow';
local base = wf(pdg=211, events=20);
base {
  modules+: {
    geant4+: {
      particle_ke_cut: 5.0,
      scoring_planes: [
        if p.id == 3 then p { ke_threshold: 1.0 } else p
        for p in base.modules.geant4.scoring_planes
      ],
    },
  },
}
EOF
(cd "$workdir" && phlex -c <(jsonnet "$workdir/pions.jsonnet"))
python3 "$workdir/check_plane_particles.py" "$workdir/gun_st_scoring_planes.root" \
  --want-history 5.0
mv "$workdir/gun_st_scoring_planes.root" "$workdir/pions.root"
rm -f "$workdir"/gun_st_scoring_planes*.root

# Replay the pions' plane_particles through Geant4 without the planes: each
# event must start from exactly its recorded tracks, where they entered the
# plane, and point back to the event it came from.
cat >"$workdir/replay.jsonnet" <<EOF
local lib = import '$here/../workflows/lib.libsonnet';
local job = lib.events(std.parseInt(std.extVar('events')));
{
  stage: lib.stage,
  driver: job.driver,
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    input: {
      cpp: 'file_source',
      stage: lib.stage,
      input_file: '$workdir/pions.root',
      product: 'plane_particles',
    },
  },
  modules: {
    geant4: lib.geant4 { seed: 20261019 },
    output: job.full_output('$workdir/replay.root', '$workdir/replay_validation.root'),
  },
}
EOF
cat >"$workdir/check_replay.py" <<'EOF'
import os
import sys

import ROOT

alive = []


def particles(v):
    return [(p.pdg_code, tuple(p.vertex), tuple(p.momentum), p.energy, p.status,
             tuple(p.mothers)) for p in v]


def by_event(path, field, key):
    # Copied out entry by entry: a view returns a reference into its buffer.
    reader = ROOT.RNTupleReader.Open("events", path)
    number = reader.GetView["std::uint64_t"]("event_number")
    header = reader.GetView["SHiP::EventHeader"]("event_header")
    mc = reader.GetView["std::vector<SHiP::MCParticle>"](field)
    parts = reader.GetView["std::vector<SHiP::SimParticle>"]("sim_particles")
    alive.extend((reader, number, header, mc, parts))
    return {
        key(number(i), header(i)): (
            particles(mc(i)),
            sorted(tuple(p.vertex) for p in parts(i) if p.parent_id == 0),
        )
        for i in range(reader.GetNEntries())
    }


stage1 = by_event(sys.argv[1], "plane_particles", lambda n, h: int(n))
stage2 = by_event(sys.argv[2], "mc_particles",
                  lambda n, h: int(h.original_event_id))
errors = []
if sorted(stage1) != sorted(stage2):
    errors.append(f"replayed events {sorted(stage2)}, expected {sorted(stage1)}")
replayed = 0
for event in sorted(stage1.keys() & stage2.keys()):
    plane, (mc, got) = stage1[event][0], stage2[event]
    if mc != plane:
        errors.append(f"event {event}: mc_particles differ from plane_particles")
    # status is the 5th entry, the vertex the 2nd
    want = sorted(p[1] for p in plane if p[4] == 1)
    if got != want:
        errors.append(f"event {event}: {len(got)} primaries, expected {len(want)}")
    replayed += len(got)

for e in errors[:20]:
    print(e)
print(f"{len(stage2)} events, {replayed} replayed tracks, {len(errors)} problem(s)")
sys.stdout.flush()
os._exit(0 if replayed > 0 and not errors else 1)
EOF
n=$(python3 "$here/count_entries.py" "$workdir/pions.root")
(cd "$workdir" && phlex -c <(jsonnet --ext-str events="$n" "$workdir/replay.jsonnet"))
python3 "$workdir/check_replay.py" "$workdir/pions.root" "$workdir/replay.root"

# Bad configurations: each must stop the job with the given message. The
# last argument is merged into the geant4 module's configuration.
expect_failure() {
  local name=$1 message=$2 geant4=$3
  cat >"$workdir/$name.jsonnet" <<EOF
local wf = import '$workflow';
wf(events=2) { modules+: { geant4+: $geant4 } }
EOF
  local out rc
  out=$(cd "$workdir" && phlex -c <(jsonnet "$workdir/$name.jsonnet") 2>&1) && rc=0 || rc=$?
  if [ "$rc" -eq 0 ] || ! grep -q "$message" <<<"$out"; then
    echo "FAIL: $name: expected the job to fail with '$message' (exit $rc)"
    echo "$out"
    exit 1
  fi
  echo "$name: fails as expected"
}

expect_failure overlapping_planes "scoring planes 'a' and 'b' overlap" \
  "{ scoring_planes: [{ name: 'a', id: 1, half_x: 100, half_y: 100, half_z: 5, position: [0, 0, 1500] },
    { name: 'b', id: 2, half_x: 100, half_y: 100, half_z: 5, position: [0, 0, 1502] }] }"
# Overlapping only in a 0.1 mm corner, which surface sampling can miss.
expect_failure corner_overlap "scoring planes 'a' and 'b' overlap" \
  "{ scoring_planes: [{ name: 'a', id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500] },
    { name: 'b', id: 2, half_x: 100, half_y: 100, half_z: 1, position: [199.9, 199.9, 1501.9] }] }"
expect_failure beyond_world "extends beyond the world" \
  "{ scoring_planes: [{ name: 'p', id: 1, half_x: 6000, half_y: 100, half_z: 1, position: [0, 0, 1500] }] }"
expect_failure unknown_key "unknown key 'kil'" \
  "{ scoring_planes: [{ name: 'p', id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500], kil: true }] }"
expect_failure missing_name "scoring plane #0 (counting from 0) has no 'name'" \
  "{ scoring_planes: [{ id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500] }] }"
expect_failure missing_id "missing 'id'" \
  "{ scoring_planes: [{ name: 'p', half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500] }] }"
expect_failure duplicate_id "id 1 used by more than one plane" \
  "{ scoring_planes: [{ name: 'a', id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500] },
    { name: 'b', id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1600] }] }"
expect_failure missing_volume "must be placed exactly once" \
  "{ scoring_planes: [{ name: 'p', id: 1, half_x: 100, half_y: 100, half_z: 1, after_volume: 'NoSuchVolume' }] }"
expect_failure two_placements "must be placed exactly once" \
  "{ scoring_planes: [{ name: 'p', id: 1, half_x: 100, half_y: 100, half_z: 1, after_volume: 'ScoringPlane' }] }"
expect_failure record_without_kill "record_particles needs kill: true" \
  "{ scoring_planes: [{ name: 'p', id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500], record_particles: true }] }"
expect_failure two_recording_planes "only one scoring plane may record particles, 'a' already does" \
  "{ scoring_planes: [{ name: 'a', id: 1, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1500], kill: true, record_particles: true },
    { name: 'b', id: 2, half_x: 100, half_y: 100, half_z: 1, position: [0, 0, 1600], kill: true, record_particles: true }] }"
expect_failure unknown_keep "unknown keep_particles 'hits'" \
  "{ keep_particles: 'hits' }"
expect_failure ancestry_with_ke_cut "set particle_ke_cut to 0" \
  "{ keep_particles: 'hit_ancestry', particle_ke_cut: 1.0 }"

# The production workflow needs the GeoModel geometry DB to run, so only
# check that it evaluates to the plane and particle settings it promises.
jsonnet --tla-code ecut=1.5 "$here/../workflows/fixed_target_production.jsonnet" |
  python3 -c '
import json, sys
g4 = json.load(sys.stdin)["modules"]["geant4"]
plane = g4["scoring_planes"][0]
sys.exit(not (g4["keep_particles"] == "hit_ancestry" and g4["particle_ke_cut"] == 0
              and g4["ke_threshold"] == 1.5 and plane["ke_threshold"] == 1.5
              and plane["after_volume"] == "/SHiP/muon_shield/magn_absorb"
              and plane["kill"] and plane["record_particles"]))
' || {
  echo "FAIL: fixed_target_production.jsonnet evaluates to unexpected geant4 settings"
  exit 1
}
echo "fixed_target_production: evaluates as expected"

echo "scoring planes passed"
