#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Writer pool: the RNTuple writer draws its fill contexts from a fixed pool
# shared across threads (issue #77), so stress the handoff: run the particle
# gun with many phlex threads but only 2 fill contexts and a 1 MiB cluster
# target (forcing frequent mid-run cluster flushes), then check that no event
# was lost or duplicated and that the file reads back cleanly.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

n=200

cat >"$workdir/write.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
local n_events = std.parseInt(std.extVar('events'));
{
  driver: lib.driver(n_events),
  sources: { gun: lib.gun },
  modules: {
    output: lib.mc_only_output(std.extVar('outfile'), std.extVar('histofile')) {
      cluster_size_mib: 1,
      fill_contexts: 2,
    },
  },
}
EOF

phlex -j 8 -c <(jsonnet -J "$workflows" \
  --ext-str events="$n" \
  --ext-str outfile="$workdir/out.root" \
  --ext-str histofile="$workdir/hist.root" \
  "$workdir/write.jsonnet")

count=$(python3 "$here/count_entries.py" "$workdir/out.root")
[ "$count" = "$n" ] || { echo "writer_pool: expected $n events, got $count"; exit 1; }

echo "writer pool passed: $n events written through 2 pooled fill contexts"
