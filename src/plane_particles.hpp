// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// plane_particles.hpp — replayable record of the tracks a scoring plane
// recorded
//
// A scoring plane with record_particles marks the tracks crossing it. At the
// end of the event these become the `plane_particles` product: one
// MCParticle per marked track, at the track's origin and with HepMC status 1,
// so that a later stage replays it from where it was produced, plus the
// track's full ancestry. Ancestors are Geant4 tracks (status
// simulation_history) up to the primary, then the primary's generator mothers
// with their own status. Nothing but the marked tracks has status 1, so
// geant4_module tracks exactly those on replay.
//
// Free of Geant4 so that it can be tested on its own.

#pragma once

#include <SHiP/MCParticle.hpp>
#include <SHiP/SimParticle.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hepmc_status.hpp"
#include "sim_particle_convert.hpp"

namespace SHiP::aegir {

/// Build the replay record of `recorded` (Geant4 track ids, in crossing
/// order; duplicates are ignored).
///
/// `find_track(id)` returns the SimParticle recorded for a track id, or
/// nullptr. `primary_source` maps the track id of every primary to the index
/// of the `generator` particle it was made from. Mother indices in the result
/// point into the result itself (SHiP::mothers_are_consistent holds).
///
/// Throws std::logic_error if an ancestor cannot be found: the record would
/// otherwise end silently in the middle of a chain.
template <typename FindTrack>
std::vector<SHiP::MCParticle> build_plane_particles(
    std::vector<int> const& recorded, FindTrack const& find_track,
    std::unordered_map<int, std::size_t> const& primary_source,
    std::vector<SHiP::MCParticle> const& generator) {
  std::vector<SHiP::MCParticle> out;
  if (recorded.empty()) {
    return out;
  }

  // What each output entry stands for, to resolve its mothers afterwards.
  struct Origin {
    bool is_track;    // a Geant4 track, else a generator entry
    std::size_t key;  // track id, or generator index
  };
  std::vector<Origin> origins;
  std::unordered_map<int, std::size_t> track_index;
  std::unordered_map<std::size_t, std::size_t> generator_index;

  auto add_generator = [&](std::size_t g, int status) {
    if (auto const it = generator_index.find(g); it != generator_index.end()) {
      return it->second;
    }
    auto mc = generator.at(g);
    mc.status = status;
    mc.mother_id = -1;
    mc.mothers.clear();
    generator_index.emplace(g, out.size());
    origins.push_back({.is_track = false, .key = g});
    out.push_back(std::move(mc));
    return out.size() - 1;
  };

  auto add_track = [&](int id, int status) {
    if (auto const it = track_index.find(id); it != track_index.end()) {
      return it->second;
    }
    // A primary is the generator particle it was made from.
    if (auto const it = primary_source.find(id); it != primary_source.end()) {
      auto const index = add_generator(it->second, status);
      track_index.emplace(id, index);
      return index;
    }
    auto const* sp = find_track(id);
    if (sp == nullptr) {
      throw std::logic_error("plane_particles: no record of track " +
                             std::to_string(id) +
                             ", an ancestor of a recorded track");
    }
    auto mc = to_mc_particle(*sp);
    mc.status = status;
    track_index.emplace(id, out.size());
    origins.push_back({.is_track = true, .key = static_cast<std::size_t>(id)});
    out.push_back(std::move(mc));
    return out.size() - 1;
  };

  for (auto const id : recorded) {
    add_track(id, hepmc::final_state);
  }

  // Resolve mothers; entries appended on the way are resolved in turn. A
  // generator ancestor marked final state was tracked itself
  // (track_all_primaries), so it is history here, not something to replay.
  auto const ancestor_status = [](int status) {
    return hepmc::is_final_state(status) ? hepmc::simulation_history : status;
  };
  for (std::size_t i = 0; i < out.size(); ++i) {
    auto const origin = origins[i];
    std::vector<std::int32_t> mothers;
    if (origin.is_track) {
      auto const* sp = find_track(static_cast<int>(origin.key));
      mothers.push_back(static_cast<std::int32_t>(
          add_track(sp->parent_id, hepmc::simulation_history)));
    } else {
      auto const& source = generator.at(origin.key);
      auto generator_mothers = source.mothers;
      if (generator_mothers.empty() && source.mother_id >= 0) {
        generator_mothers.push_back(source.mother_id);  // pre-v0.5 data
      }
      for (auto const m : generator_mothers) {
        auto const g = static_cast<std::size_t>(m);
        mothers.push_back(static_cast<std::int32_t>(
            add_generator(g, ancestor_status(generator.at(g).status))));
      }
    }
    out[i].mother_id = mothers.empty() ? -1 : mothers.front();
    out[i].mothers = std::move(mothers);
  }
  return out;
}

}  // namespace SHiP::aegir
