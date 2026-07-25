#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Verifies that aggregate simulation output is independent of scheduling:
# two multi-threaded runs and one single-threaded run of gun_st_full must
# produce identical event/hit/particle counts and energy/position sums.
# The comparison is over these per-run aggregates, not event by event, so
# compensating per-event differences would in principle go unnoticed.
# Requires the bench environment (ROOT python): pixi run -e bench ...
set -euo pipefail

EVENTS=${EVENTS:-100}
JOBS=${JOBS:-12}
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

PLUGIN_PATH="$PWD/build:$CONDA_PREFIX/lib"
CFG="$WORKDIR/cfg.json"
jsonnet -V events="$EVENTS" workflows/gun_st_full.jsonnet > "$CFG"

run() { # $1 = output subdir, $2 = -j value
  local d="$WORKDIR/$1"
  mkdir -p "$d"
  (cd "$d" && PHLEX_PLUGIN_PATH="$PLUGIN_PATH" phlex -c "$CFG" -j "$2" > run.log 2>&1)
}

run mt1 "$JOBS"
run mt2 "$JOBS"
run st 1

python3 - "$WORKDIR" <<'EOF'
import sys
import ROOT

ROOT.gROOT.SetBatch(True)
ROOT.gInterpreter.Declare('''
#include "SHiP/MCParticle.hpp"
#include "SHiP/SimHit.hpp"
#include "SHiP/SimParticle.hpp"
''')

def summarize(path):
    df = ROOT.RDataFrame("events", path)
    df = (df.Define("n_mc", "mc_particles.size()")
            .Define("n_hits", "sim_hits.size()")
            .Define("n_parts", "sim_particles.size()")
            .Define("edep", "double s=0; for (auto const& h : sim_hits) s += h.energyDeposit; return s;")
            .Define("zsum", "double s=0; for (auto const& h : sim_hits) s += h.position[2]; return s;"))
    return (int(df.Count().GetValue()), int(df.Sum("n_mc").GetValue()),
            int(df.Sum("n_hits").GetValue()), int(df.Sum("n_parts").GetValue()),
            df.Sum("edep").GetValue(), df.Sum("zsum").GetValue())

workdir = sys.argv[1]
results = {tag: summarize(f"{workdir}/{tag}/bench_gun_st_output.root")
           for tag in ("mt1", "mt2", "st")}
for tag, r in results.items():
    print(tag, r)
if results["mt1"] != results["mt2"]:
    sys.exit("FAIL: two multi-threaded runs differ")
if results["mt1"] != results["st"]:
    sys.exit("FAIL: multi-threaded and single-threaded runs differ")
print("OK: aggregate simulation output (counts, energy/position sums) "
      "is scheduling-independent")
EOF
