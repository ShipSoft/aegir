#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Source-stage check: Phlex does not pass the job's top-level `stage` on to
# sources, so every generator source block needs its own `stage` key
# (source_stage in src/mc_particle_source.hpp). Most workflows are not run in
# CI, so this guards against a block losing the key: a gun-only job must fail
# with a clear error when the key is missing or set to the reserved 'CURRENT',
# and run when it names the stage.
set -uo pipefail

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# Gun-only job with a no-op sink (no Geant4). `gun_stage` is the source's
# `stage` key; the empty string leaves it out.
cat >"$workdir/gun.jsonnet" <<'EOF'
local gun_stage = std.extVar('gun_stage');
{
  stage: 'simulation',
  driver: {
    cpp: 'generate_layers',
    layers: { event: { total: 2 } },
  },
  sources: {
    gun: {
      cpp: 'particle_gun_source',
      pdg: 13,
      p_min: 20.0,
      p_max: 20.0,
      max_theta: 0.0,
      vertex_z: -2000.0,
    } + (if gun_stage == '' then {} else { stage: gun_stage }),
  },
  modules: {
    output: { cpp: 'sim_output_module', mode: 'noop' },
  },
}
EOF

run() {
  phlex -c <(jsonnet --ext-str gun_stage="$1" "$workdir/gun.jsonnet") 2>&1
}

fail=0

for bad in '' 'CURRENT'; do
  out=$(run "$bad")
  rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "FAIL: expected failure with source stage '$bad', but phlex succeeded"
    echo "$out"
    fail=1
  elif ! grep -q "needs a 'stage' key" <<<"$out"; then
    echo "FAIL: phlex failed (exit $rc) with source stage '$bad', but not with the missing-stage error"
    echo "$out"
    fail=1
  fi
done

if ! out=$(run 'simulation'); then
  echo "FAIL: phlex failed with source stage 'simulation'"
  echo "$out"
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  exit 1
fi

echo "source-stage check passed: missing and 'CURRENT' stage fail cleanly, 'simulation' runs"
