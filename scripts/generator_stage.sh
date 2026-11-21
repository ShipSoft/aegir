#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Generator stage check: the generators label their products with the job's
# stage, and `phlex --stage` changes that label. The stage_probe plugin
# (tests/stage_probe.cpp) requires "mc_particles" and "event_header" from the
# stage given in its block, so a gun-only job with it runs only when the
# labels match. A probe expecting the wrong stage must make the job fail;
# otherwise the check proves nothing.
set -uo pipefail

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# Gun-only job with the probe. `probe_stage` is the stage the probe expects.
cat >"$workdir/gun.jsonnet" <<'EOF'
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
    },
  },
  modules: {
    probe: { cpp: 'stage_probe', stage: std.extVar('probe_stage') },
  },
}
EOF

# run <probe stage> [phlex options...]
run() {
  local probe_stage=$1
  shift
  phlex -c <(jsonnet --ext-str probe_stage="$probe_stage" "$workdir/gun.jsonnet") "$@" 2>&1
}

fail=0

expect_pass() {
  local out
  if ! out=$(run "$@"); then
    echo "FAIL: phlex failed with probe stage '$1' (options: ${*:2})"
    echo "$out"
    fail=1
  fi
}

expect_fail() {
  local out
  if out=$(run "$@"); then
    echo "FAIL: phlex succeeded with probe stage '$1' (options: ${*:2}), expected a failure"
    echo "$out"
    fail=1
  fi
}

expect_pass simulation
expect_pass foo --stage foo
expect_fail simulation --stage foo
expect_fail foo

if [ "$fail" -ne 0 ]; then
  exit 1
fi

echo "generator-stage check passed: generator products carry the job's stage, --stage included"
