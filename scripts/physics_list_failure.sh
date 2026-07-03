#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Clean-failure check: an unknown physics_list must make the Geant4 module fail
# with a catchable, clear error rather than aborting the process. Geant4's
# G4PhysListFactory::GetReferencePhysList raises a fatal G4Exception (SIGABRT)
# for an unknown list, so the module validates the name up front and throws a
# std::runtime_error instead.
#
# The module's std::call_once init is sticky: the first failure is captured and
# rethrown on every later simulate call, so phlex drains the events already in
# flight, reports the error, and exits — instead of a retry constructing a
# second G4MTRunManager and hitting the fatal "G4RunManager constructed twice"
# abort. We drive a few events with -j > 1 so several events pass through the
# sticky error, and check below that none of them triggered that abort.
set -uo pipefail

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat >"$workdir/bad_physics.jsonnet" <<'EOF'
{
  driver: {
    cpp: 'generate_layers',
    layers: { event: { total: 8 } },
  },
  sources: {
    geometry: { cpp: 'geometry_builtin_provider' },
    field: { cpp: 'field_null_provider' },
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
    geant4: {
      cpp: 'geant4_module',
      physics_list: 'NO_SUCH_LIST',
      concurrency: 4,
    },
    output: { cpp: 'sim_output_module', mode: 'noop' },
  },
}
EOF

out=$(phlex -c <(jsonnet "$workdir/bad_physics.jsonnet") -j 4 2>&1)
rc=$?

if [ "$rc" -eq 0 ]; then
  echo "FAIL: expected failure on unknown physics list, but phlex succeeded"
  echo "$out"
  exit 1
fi

if ! grep -q "Unknown physics list" <<<"$out"; then
  echo "FAIL: phlex failed (exit $rc) but not with the clean 'Unknown physics list' error"
  echo "$out"
  exit 1
fi

# Re-constructing the run manager aborts via G4Exception (SIGABRT -> exit 134)
# with "constructed twice"; the sticky-failure path never does.
if [ "$rc" -eq 134 ] || grep -qi "constructed twice" <<<"$out"; then
  echo "FAIL: init aborted the run manager instead of failing cleanly (exit $rc)"
  echo "$out"
  exit 1
fi

echo "physics-list check passed: unknown list fails cleanly, no run-manager abort"
