#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Check the file_summary sim_output_module writes next to the events RNTuple.

    check_file_summary.py <file.root> <mode> <filter_empty> <n_requested> \\
        <n_processed> <n_written> <max_event_number>
    check_file_summary.py --incomplete <file.root> <n_requested>

Pass `null` as <n_requested> for a workflow that does not pass its event total
on to the output module, and as <max_event_number> when no event was written.
--incomplete checks a job that stopped early, whose exact counts depend on which
events were in flight: some events were processed, but fewer than requested.
Exits non-zero, printing every mismatch, if the summary is missing or does not
match.
"""

import json
import os
import sys

import ROOT


def load(path):
    """Return (summary, None), or (None, error) if it cannot be read."""
    file = ROOT.TFile.Open(path)
    if not file or file.IsZombie():
        return None, f"cannot open '{path}'"
    obj = file.Get("file_summary")
    if not obj:
        return None, f"no file_summary in '{path}'"
    return json.loads(str(obj.GetString())), None


def check(
    path, mode, filter_empty, n_requested, n_processed, n_written, max_event_number
):
    summary, error = load(path)
    if error:
        return [error]
    want = {
        "schema_version": 1,
        "producer": "aegir",
        "mode": mode,
        "filter_empty": filter_empty,
        "n_events_requested": n_requested,
        "n_events_processed": n_processed,
        "n_events_written": n_written,
        "max_event_number_saved": max_event_number,
    }
    return [
        f"file_summary {key}: got {summary.get(key, '<missing>')!r}, expected {value!r}"
        for key, value in want.items()
        if key not in summary or summary[key] != value
    ]


def check_incomplete(path, n_requested):
    summary, error = load(path)
    if error:
        return [error]
    errors = []
    requested = summary.get("n_events_requested")
    if requested != n_requested:
        errors.append(
            f"file_summary n_events_requested: got {requested!r}, "
            f"expected {n_requested!r}"
        )
    processed = summary.get("n_events_processed")
    if not isinstance(processed, int) or not 0 < processed < n_requested:
        errors.append(
            f"file_summary n_events_processed: got {processed!r}, "
            f"expected between 1 and {n_requested - 1}"
        )
    written = summary.get("n_events_written")
    if written != processed:
        errors.append(
            f"file_summary n_events_written: got {written!r}, expected {processed!r}"
        )
    return errors


def int_or_null(arg):
    return None if arg == "null" else int(arg)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--incomplete":
        errors = check_incomplete(sys.argv[2], int(sys.argv[3]))
    elif len(sys.argv) == 8:
        errors = check(
            sys.argv[1],
            sys.argv[2],
            sys.argv[3] == "true",
            int_or_null(sys.argv[4]),
            int(sys.argv[5]),
            int(sys.argv[6]),
            int_or_null(sys.argv[7]),
        )
    else:
        print(
            f"usage: {sys.argv[0]} <file.root> <mode> <true|false> "
            "<n_requested|null> <n_processed> <n_written> "
            f"<max_event_number|null>\n"
            f"       {sys.argv[0]} --incomplete <file.root> <n_requested>",
            file=sys.stderr,
        )
        sys.exit(2)
    for error in errors:
        print(error)
    sys.stdout.flush()
    # Skip interpreter shutdown to dodge a PyROOT teardown crash.
    os._exit(1 if errors else 0)
