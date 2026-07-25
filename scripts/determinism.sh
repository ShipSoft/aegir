#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Determinism check: the particle-gun generator seeds a counter-based RNG
# from a base seed and the event number, so two runs with the same seed
# must produce identical output, a run with a different seed must not, and
# a run without a seed must draw and log one. This exercises the Philox
# path and guards against RNG regressions.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat >"$workdir/gun.jsonnet" <<'EOF'
local n_events = std.parseInt(std.extVar('events'));
local seed = std.parseInt(std.extVar('seed'));
{
  driver: {
    cpp: 'generate_layers',
    layers: { event: { total: n_events } },
  },
  sources: {
    gun: {
      cpp: 'particle_gun_source',
      pdg: 13,
      p_min: 10.0,
      p_max: 100.0,
      max_theta: 0.1,
      vertex_z: -500.0,
      // seed < 0 means "leave unset": the source draws and logs one.
      [if seed >= 0 then 'seed']: seed,
    },
  },
  modules: {
    output: {
      cpp: 'sim_output_module',
      mode: 'mc_only',
      rntuple_file: std.extVar('outfile'),
      histo_file: std.extVar('histofile'),
    },
  },
}
EOF

run() {
  phlex -c <(jsonnet \
    --ext-str events=100 \
    --ext-str seed="$3" \
    --ext-str outfile="$1" \
    --ext-str histofile="$2" \
    "$workdir/gun.jsonnet")
}

# Same seed twice: identical output.
run "$workdir/det1.root" "$workdir/hist1.root" 20260703
run "$workdir/det2.root" "$workdir/hist2.root" 20260703
python3 "$here/compare_histograms.py" "$workdir/hist1.root" "$workdir/hist2.root"

# A different seed: output must differ.
run "$workdir/det3.root" "$workdir/hist3.root" 12345
if python3 "$here/compare_histograms.py" "$workdir/hist1.root" "$workdir/hist3.root" >/dev/null 2>&1; then
  echo "determinism check FAILED: different seeds produced identical output" >&2
  exit 1
fi

# No seed: the source draws one and logs it for later reproduction.
# Capture to a file rather than piping into grep -q: its early exit would
# SIGPIPE phlex and trip pipefail even on a match.
run "$workdir/det4.root" "$workdir/hist4.root" -1 >"$workdir/unseeded.log" 2>&1
if ! grep -q "particle_gun: no seed configured — drew random seed" \
  "$workdir/unseeded.log"; then
  echo "determinism check FAILED: unseeded run did not log a drawn seed" >&2
  exit 1
fi

echo "determinism check passed: seeded runs reproduce, unseeded runs draw"
