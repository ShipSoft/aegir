#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# beam_spot_module: a particle gun fires along z from z = -500 mm, the
# beam_spot transform moves its events, and the output (mc_only, reading the
# transform's particles via particles_from) is checked:
#   - a fixed centre shifts every vertex by exactly that amount and leaves
#     the momenta alone;
#   - with a sweep and keep_time, each offset lies on the sweep circle at the
#     angle given by the spill time added to the particle's time;
#   - the spot has the mean and RMS of a uniform +-a plus a Gaussian sigma;
#   - with a source distance the momentum turns to the offset over the
#     distance, and the vertex moves along that direction from reference_z;
#     the sweep's part of the offset can have its own distance;
#   - the same seed gives the same events, another seed different ones;
#   - a negative sigma stops the job.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat >"$workdir/spot.jsonnet" <<'EOF'
function(out, events, spot, seed=1) {
  stage: 'simulation',
  driver: { cpp: 'generate_layers', layers: { event: { total: events } } },
  sources: {
    gun: {
      cpp: 'particle_gun_source',
      pdg: 13,
      p_min: 10.0,
      p_max: 10.0,
      max_theta: 0.0,
      vertex_z: -500.0,
      seed: 7,
    },
  },
  modules: {
    beam_spot: { cpp: 'beam_spot_module', seed: seed } + spot,
    output: {
      cpp: 'sim_output_module',
      mode: 'mc_only',
      particles_from: 'beam_spot',
      rntuple_file: out,
      histo_file: out + '.histo.root',
    },
  },
}
EOF

# Prints the per-event (event_number, x, y, z, px, py, pz, t) of the single
# muon as JSON, sorted by event number.
cat >"$workdir/dump.py" <<'EOF'
import json
import os
import sys

import ROOT

reader = ROOT.RNTupleReader.Open("events", sys.argv[1])
particles = reader.GetView["std::vector<SHiP::MCParticle>"]("mc_particles")
numbers = reader.GetView["std::uint64_t"]("event_number")
rows = []
for i in range(reader.GetNEntries()):
    p = particles(i)[0]
    rows.append([int(numbers(i)), *p.vertex, *p.momentum, p.time])
json.dump(sorted(rows), sys.stdout)
sys.stdout.flush()
os._exit(0)
EOF

run() {  # run <name> <events> <spot-config> [seed]
  phlex -c <(jsonnet --tla-str out="$workdir/$1.root" --tla-code events="$2" \
    --tla-code spot="$3" --tla-code seed="${4:-1}" "$workdir/spot.jsonnet") \
    >"$workdir/$1.log" 2>&1 || { cat "$workdir/$1.log"; return 1; }
  python3 "$workdir/dump.py" "$workdir/$1.root" >"$workdir/$1.json"
}

run fixed 20 "{centre: {x: 3.0, y: -4.0}}"
run sweep 200 "{keep_time: true, sweep: {radius_x: 50.0, radius_y: 40.0, frequency: -4.0, phase: -1.5707963267948966}}"
run spot 4000 "{spot: {half_width_x: 20.0, sigma_x: 5.0, sigma_y: 15.0}}"
run angle 50 "{centre: {x: 30.0, y: -20.0}, source_distance: {x: 100.0, y: 150.0}}"
run sweep_angle 50 "{keep_time: true, centre: {x: 10.0}, sweep: {radius_x: 50.0, frequency: 3.0, source_distance_x: 120.0}, source_distance: {x: 100.0}}"
run spot_again 4000 "{spot: {half_width_x: 20.0, sigma_x: 5.0, sigma_y: 15.0}}"
run spot_seed2 4000 "{spot: {half_width_x: 20.0, sigma_x: 5.0, sigma_y: 15.0}}" 2

python3 - "$workdir" <<'EOF'
import json
import math
import sys

d = sys.argv[1]
load = lambda name: json.load(open(f"{d}/{name}.json"))
errors = []


def check(what, ok):
    if not ok:
        errors.append(what)


close = lambda a, b, tol=1e-9: abs(a - b) <= tol * max(1.0, abs(b))

fixed = load("fixed")
check("fixed: 20 events", len(fixed) == 20)
for _, x, y, z, px, py, pz, t in fixed:
    check(f"fixed: vertex ({x}, {y}, {z})", close(x, 3.0) and close(y, -4.0) and close(z, -500.0))
    check(f"fixed: momentum ({px}, {py}, {pz})", px == 0 and py == 0 and close(pz, 10.0))
    check(f"fixed: time {t}", t == 0)

ns_per_s = 1e9  # the spill time is in s, MCParticle times in ns
for n, x, y, _, *_rest, t in load("sweep"):
    phase = 2 * math.pi * -4.0 * (t / ns_per_s) - math.pi / 2
    check(f"sweep: event {n} spill time {t} ns", 0 <= t < ns_per_s)
    check(
        f"sweep: event {n} at ({x}, {y}), expected angle {phase}",
        close(x, 50.0 * math.cos(phase), 1e-6) and close(y, 40.0 * math.sin(phase), 1e-6),
    )


def moments(values):
    m = sum(values) / len(values)
    return m, math.sqrt(sum((v - m) ** 2 for v in values) / len(values))


spot = load("spot")
mx, sx = moments([r[1] for r in spot])
my, sy = moments([r[2] for r in spot])
want_sx = math.sqrt(20.0**2 / 3 + 5.0**2)
check(f"spot: x mean {mx}", abs(mx) < 4 * want_sx / math.sqrt(len(spot)))
check(f"spot: y mean {my}", abs(my) < 4 * 15.0 / math.sqrt(len(spot)))
check(f"spot: x rms {sx}, expected {want_sx}", abs(sx / want_sx - 1) < 0.05)
check(f"spot: y rms {sy}, expected 15", abs(sy / 15.0 - 1) < 0.05)
check("spot: |x| bounded by a + 6 sigma", max(abs(r[1]) for r in spot) < 20.0 + 6 * 5.0)
check("spot: same seed, same events", load("spot_again") == spot)
check("spot: another seed, other events", load("spot_seed2") != spot)

mm_per_m = 1000.0
tx, ty = 30.0 / (100.0 * mm_per_m), -20.0 / (150.0 * mm_per_m)
for n, x, y, z, px, py, pz, _ in load("angle"):
    check(f"angle: event {n} slope ({px / pz}, {py / pz})", close(px / pz, tx, 1e-9) and close(py / pz, ty, 1e-9))
    check(f"angle: event {n} |p|", close(math.sqrt(px * px + py * py + pz * pz), 10.0))
    check(f"angle: event {n} vertex ({x}, {y})", close(x, 30.0 + tx * -500.0) and close(y, -20.0 + ty * -500.0))

for n, x, y, z, px, py, pz, t in load("sweep_angle"):
    sweep = 50.0 * math.cos(2 * math.pi * 3.0 * (t / ns_per_s))
    want = 10.0 / (100.0 * mm_per_m) + sweep / (120.0 * mm_per_m)
    check(f"sweep_angle: event {n} slope {px / pz}, expected {want}", close(px / pz, want, 1e-6))
    check(f"sweep_angle: event {n} x {x}", close(x, 10.0 + sweep + want * -500.0, 1e-6))
    check(f"sweep_angle: event {n} y slope {py / pz}", py == 0)

for e in errors[:20]:
    print(e)
sys.exit(1 if errors else 0)
EOF

if phlex -c <(jsonnet --tla-str out="$workdir/bad.root" --tla-code events=1 \
  --tla-code spot="{spot: {sigma_x: -1.0}}" "$workdir/spot.jsonnet") >"$workdir/bad.log" 2>&1; then
  echo "a negative sigma should have stopped the job"
  exit 1
fi
grep -qF "sigma, half_width and sweep radius must be >= 0" "$workdir/bad.log" || {
  echo "unexpected error for a negative sigma:"
  cat "$workdir/bad.log"
  exit 1
}

echo "beam_spot passed: shift, sweep, spot, angles, seeding and errors"
