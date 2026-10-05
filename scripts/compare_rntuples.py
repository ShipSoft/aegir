#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Check that two output files hold the same entries in every RNTuple.

Used by the thread-independence check: a job run with different thread
counts must write the same events. The multi-threaded writer stores events
in whatever order they finish, so each RNTuple's entries are compared as a
sorted list. An entry holds everything written for one event (generator
particles and, in full output, the Geant4 result), so an event paired with
another event's content shows up as a difference. Exits non-zero on any
mismatch.
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
    add(p.pdgCode); add(p.vertex); add(p.momentum); add(p.energy);
    add(p.time); add(p.motherId); add(p.status);
    add(static_cast<std::uint64_t>(p.mothers.size()));
    for (auto m : p.mothers) add(m);
  }
  void add(SHiP::SimHit const& s) {
    add(s.detectorId); add(s.geometryNodeId); add(s.trackId); add(s.pdgCode);
    add(s.position); add(s.momentum); add(s.energyDeposit); add(s.time);
    add(s.pathLength);
  }
  void add(SHiP::SimParticle const& s) {
    add(s.trackId); add(s.parentId); add(s.pdgCode); add(s.vertex);
    add(s.endpoint); add(s.momentum); add(s.energy); add(s.time);
    add(s.creatorProcess);
  }
  template <typename T>
  void add(std::vector<T> const& v) {
    add(static_cast<std::uint64_t>(v.size()));
    for (auto const& x : v) add(x);
  }
};

std::vector<std::uint64_t> entry_hashes(std::string const& name,
                                        std::string const& path) {
  auto reader = ROOT::RNTupleReader::Open(name, path);
  std::vector<std::uint64_t> hashes;
  auto const& desc = reader->GetDescriptor();
  auto has = [&](char const* field) {
    return desc.FindFieldId(field) != ROOT::kInvalidDescriptorId;
  };
  if (name == "events" && has("mc_particles")) {
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
      hashes.push_back(h.h);
    }
    return hashes;
  }
  for (auto i : reader->GetEntryRange()) {
    std::ostringstream out;
    reader->Show(i, out);
    hashes.push_back(std::hash<std::string>{}(out.str()));
  }
  return hashes;
}
}  // namespace aegir_compare
"""
)


def entries(path):
    """Map each RNTuple in the file to its sorted list of entry hashes."""
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
        result[name] = sorted(ROOT.aegir_compare.entry_hashes(name, path))
    return result


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
