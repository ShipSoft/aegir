#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Print the number of entries (events) in a ROOT RNTuple or TTree.

Lets you drive file_source over a whole file without knowing the event count
up front: use the printed number as the `events` total of the workflow, e.g.

    n=$(pixi run count_entries input.root)
    phlex -c <(jsonnet --ext-str events="$n" --ext-str infile=input.root \\
        workflows/file_read.jsonnet)

The name defaults to "events". A file without an "events" object but with a
single TTree (the beam-line files read by beam_file_source) reports that
TTree's entries.
"""

import os
import sys

import ROOT


def count(path, name=None):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        raise SystemExit(f"cannot open {path}")
    if name is None:
        if f.GetKey("events"):
            name = "events"
        else:
            trees = {
                k.GetName() for k in f.GetListOfKeys() if k.GetClassName() == "TTree"
            }
            if len(trees) != 1:
                raise SystemExit(
                    f"{path}: no 'events' and {len(trees)} TTrees; name one"
                )
            name = trees.pop()
    key = f.GetKey(name)
    if key and key.GetClassName() == "TTree":
        return f.Get(name).GetEntries()
    reader = ROOT.RNTupleReader.Open(name, path)
    return reader.GetNEntries()


if __name__ == "__main__":
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print(
            f"usage: {sys.argv[0]} <file.root> [ntuple_or_tree_name]", file=sys.stderr
        )
        sys.exit(2)
    print(count(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else None))
    sys.stdout.flush()
    # Skip interpreter shutdown to dodge a PyROOT RNTuple teardown crash.
    os._exit(0)
