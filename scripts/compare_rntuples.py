#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Check that two output files hold the same entries in every RNTuple.

Used by the thread-independence check: a job run with different thread
counts must write the same events. The multi-threaded writer stores events
in whatever order they finish, so aegir's "events" RNTuple is compared
event by event, keyed by the phlex event number each entry carries. An entry
holds everything written for one event (generator particles and, in full
output, the Geant4 result), so both an event paired with another event's
content and the same events written under different event numbers show up
as differences. Other RNTuples have no event number and are compared as
sorted lists of entries. Exits non-zero on any mismatch.
"""

import os
import sys
from collections import Counter

import ROOT

# One hash per entry, computed in C++: going through Python, or through
# RNTupleReader::Show's JSON, takes minutes for Geant4 output. aegir's own
# "events" RNTuple is hashed field by field from its typed values; any other
# RNTuple falls back to hashing the entry as Show prints it.
ROOT.gInterpreter.Declare(
    """
#include <ROOT/RNTupleReader.hxx>
#include <SHiP/EventHeader.hpp>
#include <SHiP/MCParticle.hpp>
#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace aegir_compare {
struct Hasher {
  std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
  void add(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (8 * i)) & 0xFF;
      h *= 1099511628211ULL;
    }
  }
  void add(double v) { add(std::bit_cast<std::uint64_t>(v)); }
  void add(std::int64_t v) { add(static_cast<std::uint64_t>(v)); }
  void add(std::int32_t v) { add(static_cast<std::uint64_t>(v)); }
  template <typename T, std::size_t N>
  void add(std::array<T, N> const& a) { for (auto x : a) add(x); }
  void add(SHiP::MCParticle const& p) {
    add(p.pdg_code); add(p.vertex); add(p.momentum); add(p.energy);
    add(p.time); add(p.mother_id); add(p.status);
    add(static_cast<std::uint64_t>(p.mothers.size()));
    for (auto m : p.mothers) add(m);
  }
  void add(SHiP::SimHit const& s) {
    add(s.detector_id); add(s.geometry_node_id); add(s.track_id); add(s.pdg_code);
    add(s.position); add(s.momentum); add(s.energy_deposit); add(s.time);
    add(s.path_length);
  }
  void add(SHiP::SimParticle const& s) {
    add(s.track_id); add(s.parent_id); add(s.pdg_code); add(s.vertex);
    add(s.endpoint); add(s.momentum); add(s.energy); add(s.time);
    add(s.creator_process);
  }
  template <typename T>
  void add(std::vector<T> const& v) {
    add(static_cast<std::uint64_t>(v.size()));
    for (auto const& x : v) add(x);
  }
};

// One hash per entry; for aegir's "events" RNTuple also each entry's phlex
// event number (`keyed`), for everything else entry order only.
struct Entries {
  bool keyed = false;
  std::vector<std::uint64_t> numbers;
  std::vector<std::uint64_t> hashes;
};

Entries entry_hashes(std::string const& name, std::string const& path) {
  auto reader = ROOT::RNTupleReader::Open(name, path);
  Entries result;
  auto& hashes = result.hashes;
  auto const& desc = reader->GetDescriptor();
  auto has = [&](char const* field) {
    return desc.FindFieldId(field) != ROOT::kInvalidDescriptorId;
  };
  if (name == "events" && has("mc_particles")) {
    if (!has("event_number")) {
      throw std::runtime_error(path + ": events has no event_number field");
    }
    result.keyed = true;
    auto number = reader->GetView<std::uint64_t>("event_number");
    auto mc = reader->GetView<std::vector<SHiP::MCParticle>>("mc_particles");
    std::optional<ROOT::RNTupleView<SHiP::EventHeader>> header;
    std::optional<ROOT::RNTupleView<std::vector<SHiP::SimHit>>> hits;
    std::optional<ROOT::RNTupleView<std::vector<SHiP::SimParticle>>> parts;
    if (has("event_header")) header.emplace(reader->GetView<SHiP::EventHeader>("event_header"));
    if (has("sim_hits")) hits.emplace(reader->GetView<std::vector<SHiP::SimHit>>("sim_hits"));
    if (has("sim_particles")) parts.emplace(reader->GetView<std::vector<SHiP::SimParticle>>("sim_particles"));
    for (auto i : reader->GetEntryRange()) {
      Hasher h;
      h.add(mc(i));
      if (header) { h.add((*header)(i).weight); h.add((*header)(i).original_event_id); }
      if (hits) h.add((*hits)(i));
      if (parts) h.add((*parts)(i));
      result.numbers.push_back(number(i));
      hashes.push_back(h.h);
    }
    return result;
  }
  for (auto i : reader->GetEntryRange()) {
    std::ostringstream out;
    reader->Show(i, out);
    hashes.push_back(std::hash<std::string>{}(out.str()));
  }
  return result;
}
}  // namespace aegir_compare
"""
)


def keyed_or_sorted(name, path):
    """Return {event number: hash} for "events", else sorted entry hashes."""
    e = ROOT.aegir_compare.entry_hashes(name, path)
    if not e.keyed:
        return sorted(e.hashes)
    by_number = dict(zip(e.numbers, e.hashes))
    if len(by_number) != len(e.hashes):
        raise RuntimeError(f"{path}: {name} repeats event numbers")
    return by_number


def entries(path):
    """Map each RNTuple in the file to its entries (see keyed_or_sorted)."""
    file = ROOT.TFile.Open(path)
    if not file or file.IsZombie():
        raise RuntimeError(f"cannot open {path}")
    names = sorted(
        key.GetName()
        for key in file.GetListOfKeys()
        if key.GetClassName() == "ROOT::RNTuple"
    )
    file.Close()
    result = {}
    for name in names:
        result[name] = keyed_or_sorted(name, path)
    return result


def differing_events(a, b):
    """Event numbers whose entries differ or exist in only one file."""
    return sorted(n for n in a.keys() | b.keys() if a.get(n) != b.get(n))


def compare(path_a, path_b):
    a = entries(path_a)
    b = entries(path_b)
    if a.keys() != b.keys():
        print(f"RNTuples differ: {sorted(a)} vs {sorted(b)}")
        return False
    if not a:
        print(f"no RNTuples in {path_a}")
        return False
    ok = True
    for name in a:
        if len(a[name]) != len(b[name]):
            print(f"{name}: {len(a[name])} vs {len(b[name])} entries")
            ok = False
        elif isinstance(a[name], dict):
            differing = differing_events(a[name], b[name])
            if differing:
                shown = ", ".join(str(n) for n in differing[:10])
                more = ", ..." if len(differing) > 10 else ""
                print(
                    f"{name}: {len(differing)} of {len(a[name])} events differ"
                    f" (event numbers {shown}{more})"
                )
                ok = False
        elif a[name] != b[name]:
            differing = sum((Counter(a[name]) - Counter(b[name])).values())
            print(f"{name}: {differing} of {len(a[name])} entries differ")
            ok = False
    if ok:
        print(f"identical: {', '.join(f'{n} ({len(a[n])})' for n in a)}")
    return ok


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <a.root> <b.root>", file=sys.stderr)
        sys.exit(2)
    status = 0 if compare(sys.argv[1], sys.argv[2]) else 1
    sys.stdout.flush()
    # Skip interpreter teardown: PyROOT's RNTuple readers can crash in it.
    os._exit(status)
