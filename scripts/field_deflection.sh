#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Field deflection: generate a constant 0.5 T By map with
# generate_constant_fieldmap, run gun_st_field_smoke through field_map_provider,
# and check the 20 GeV μ− lands where the analytic circle puts it at each
# scoring plane. Catches a field-service update that breaks map loading or
# evaluation, which the no-field tests would not notice.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# The map covers the builtin geometry's World box (±5 m in x/y, ±20 m in z).
generate_constant_fieldmap "$workdir/world_05T_y.root" world_05T_y \
  0 0.5 0 -5000 5000 -5000 5000 -20000 20000

# The workflow names its map and output files relative to the working directory.
(cd "$workdir" && phlex -c <(jsonnet "$workflows/gun_st_field_smoke.jsonnet"))

# Mean x of the primary muon (track 1) at each scoring plane, compared with the
# exact circle x0 + R - sqrt(R^2 - L^2), R = p / (0.3 B), L measured from the
# gun at z = -2 m. Energy loss and scattering in air and silicon shift the mean
# by well under 1%. os._exit dodges a PyROOT RNTuple teardown crash.
cat >"$workdir/check_deflection.py" <<'EOF'
import math
import os
import sys

import ROOT

path = sys.argv[1]
x0, z_gun = 200.0, -2000.0  # gun offset and vertex [mm]
radius = 20.0 / (0.3 * 0.5) * 1000.0  # 20 GeV/c in 0.5 T [mm]

reader = ROOT.RNTupleReader.Open("events", path)
view = reader.GetView["std::vector<SHiP::SimHit>"]("sim_hits")
xs = {}
for i in range(reader.GetNEntries()):
    seen = set()
    for h in view(i):
        if h.trackId != 1 or h.pdgCode != 13:
            continue
        plane = round(h.position[2] / 1000.0)  # planes sit at whole metres
        if plane in seen:
            continue
        seen.add(plane)
        xs.setdefault(plane, []).append(h.position[0])

ok = len(xs) == 5
if not ok:
    print(f"expected muon hits on 5 planes, got {sorted(xs)}")
for plane in sorted(xs):
    length = plane * 1000.0 - z_gun
    expected = radius - math.sqrt(radius**2 - length**2)
    got = sum(xs[plane]) / len(xs[plane]) - x0
    good = abs(got - expected) <= 0.01 * expected
    ok = ok and good
    print(
        f"z = {plane} m: deflection {got:.1f} mm, expected {expected:.1f} mm"
        f" ({len(xs[plane])} hits){'' if good else '  <-- off by more than 1%'}"
    )
sys.stdout.flush()
os._exit(0 if ok else 1)
EOF

python3 "$workdir/check_deflection.py" "$workdir/gun_st_field_smoke.root"
echo "field deflection passed: muon follows the analytic circle in 0.5 T"
