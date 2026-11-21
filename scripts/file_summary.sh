#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# File summary: the writer records its event counts in a file_summary object
# next to the events RNTuple. Check them for a complete job, a workflow that
# does not pass its event total on, filter_empty keeping every event and keeping
# none, and a job that stops early.
# Relies on PHLEX_PLUGIN_PATH being set (activate.sh does this under `pixi run`).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

check="$here/check_file_summary.py"
n=20

cat >"$workdir/gun.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
local events = std.parseInt(std.extVar('events'));
local requested = if std.extVar('pass_total') == 'true' then events;
{
  stage: lib.stage,
  driver: lib.driver(events),
  sources: { gun: lib.gun },
  modules: {
    output: lib.mc_only_output(std.extVar('outfile'), std.extVar('outfile') + '.histo.root', requested),
  },
}
EOF

cat >"$workdir/filter.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
local job = lib.events(std.parseInt(std.extVar('events')));
{
  stage: lib.stage,
  driver: job.driver,
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    gun: lib.gun { pdg: std.parseInt(std.extVar('pdg')) },
  },
  modules: {
    geant4: lib.geant4,
    output: job.full_output(std.extVar('outfile'), std.extVar('outfile') + '.histo.root') {
      filter_empty: true,
    },
  },
}
EOF

# The source runs out after 10 events, so the job stops short of the driver's 20.
cat >"$workdir/exhaust.jsonnet" <<'EOF'
local lib = import 'lib.libsonnet';
local job = lib.events(std.parseInt(std.extVar('events')));
{
  stage: lib.stage,
  driver: job.driver,
  sources: {
    pythia8: lib.pythia8 { parallel: true, num_threads: 2, num_events: 10 },
  },
  modules: {
    output: job.mc_only_output(std.extVar('outfile'), std.extVar('outfile') + '.histo.root'),
  },
}
EOF

run() { # $1 = workflow, $2 = output file, remaining = extra jsonnet flags
  local workflow=$1 outfile=$2
  shift 2
  phlex -c <(jsonnet -J "$workflows" --ext-str events="$n" \
    --ext-str outfile="$outfile" "$@" "$workdir/$workflow.jsonnet")
}

run gun "$workdir/complete.root" --ext-str pass_total=true
python3 "$check" "$workdir/complete.root" mc_only false "$n" "$n" "$n" $((n - 1))

run gun "$workdir/no_total.root" --ext-str pass_total=false
python3 "$check" "$workdir/no_total.root" mc_only false null "$n" "$n" $((n - 1))

# Muons always leave hits, so filter_empty keeps every event.
run filter "$workdir/kept.root" --ext-str pdg=13
python3 "$check" "$workdir/kept.root" full true "$n" "$n" "$n" $((n - 1))

# Neutrinos leave none, so it keeps no event and the maximum is null.
run filter "$workdir/dropped.root" --ext-str pdg=14
python3 "$check" "$workdir/dropped.root" full true "$n" "$n" 0 null

if run exhaust "$workdir/early.root" 2>"$workdir/early.log"; then
  echo "FAIL: expected the job to stop early, but phlex succeeded"
  exit 1
fi
python3 "$check" --incomplete "$workdir/early.root" "$n" || {
  cat "$workdir/early.log"
  exit 1
}

echo "file summary passed: counts correct for complete, filtered and stopped jobs"
