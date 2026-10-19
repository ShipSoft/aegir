// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// plane_particles_test.cpp — build_plane_particles on a hand-built event

#include "plane_particles.hpp"

#include <SHiP/MCParticle.hpp>
#include <SHiP/SimParticle.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <print>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "hepmc_status.hpp"

namespace {

// Counts and reports failed checks.
struct Checker {
  int failures = 0;

  void operator()(bool ok, char const* what) {
    if (!ok) {
      std::println(stderr, "FAILED: {}", what);
      ++failures;
    }
  }
};

SHiP::MCParticle generator_particle(int pdg, int status,
                                    std::vector<std::int32_t> mothers) {
  SHiP::MCParticle p;
  p.pdg_code = pdg;
  p.status = status;
  p.mother_id = mothers.empty() ? -1 : mothers.front();
  p.mothers = std::move(mothers);
  p.momentum = {0, 0, 10};
  p.energy = 10;
  return p;
}

SHiP::SimParticle track(int id, int parent, int pdg) {
  SHiP::SimParticle p;
  p.track_id = id;
  p.parent_id = parent;
  p.pdg_code = pdg;
  p.vertex = {0, 0, 100.0 * id};
  p.momentum = {0, 0, 1};
  p.energy = 0.5;
  return p;
}

}  // namespace

int main() {
  using SHiP::aegir::hepmc::final_state;
  using SHiP::aegir::hepmc::simulation_history;
  Checker check;

  // Generator record: two beams, a decayed pion, and three final-state
  // particles; the kaon (5) has only the pre-v0.5 mother_id.
  std::vector<SHiP::MCParticle> const generator{
      generator_particle(2212, SHiP::aegir::hepmc::beam, {}),        // 0
      generator_particle(2212, SHiP::aegir::hepmc::beam, {}),        // 1
      generator_particle(211, SHiP::aegir::hepmc::decayed, {0, 1}),  // 2
      generator_particle(-13, final_state, {2}),                     // 3
      generator_particle(2212, final_state, {0, 1}),                 // 4
      [] {
        auto k = generator_particle(321, final_state, {});
        k.mother_id = 2;
        return k;
      }(),  // 5
  };
  // Primaries 1, 2, 3 were made from generator entries 3, 4, 5.
  std::unordered_map<int, std::size_t> const primary_source{
      {1, 3}, {2, 4}, {3, 5}};

  // Secondaries: 4 from 2, 5 and 6 from 4, 7 from 3. Muon 5 stands in for a
  // track below particle_ke_cut, found only in the lineage store.
  double const m_mu = 0.1056583755;
  auto mu = track(5, 4, 13);
  mu.energy = std::sqrt(1 + (m_mu * m_mu)) - m_mu;  // |p| = 1 GeV
  std::vector<SHiP::SimParticle> const tracks{
      track(4, 2, 211), mu, track(6, 4, 22), track(7, 3, 211)};
  auto const find_track = [&tracks](int id) -> SHiP::SimParticle const* {
    auto const it = std::ranges::find(tracks, id, &SHiP::SimParticle::track_id);
    return it == tracks.end() ? nullptr : &*it;
  };

  check(SHiP::aegir::build_plane_particles({}, find_track, primary_source,
                                           generator)
            .empty(),
        "nothing recorded gives an empty record");

  auto const out = SHiP::aegir::build_plane_particles(
      {5, 1, 6, 5, 7}, find_track, primary_source, generator);

  check(SHiP::mothers_are_populated(out), "mother links are consistent");
  // 4 recorded; tracks 4 and primaries 2 and 3 as history; generator 0, 1, 2.
  check(out.size() == 10, "every ancestor appears exactly once");
  check(std::ranges::count(out, final_state, &SHiP::MCParticle::status) == 4,
        "only the recorded tracks have status 1");
  for (std::size_t i = 0; i < 4; ++i) {
    check(out[i].status == final_state, "recorded tracks come first");
  }

  // The recorded muon is at its origin, with its total energy.
  check(out[0].pdg_code == 13 && out[0].vertex[2] == 500, "muon at its origin");
  check(std::abs(out[0].energy - std::sqrt(1 + (m_mu * m_mu))) < 1e-12,
        "muon total energy");

  auto const& pion = out[out[0].mother_id];
  check(pion.pdg_code == 211 && pion.status == simulation_history,
        "muon's mother is the Geant4 pion");
  auto const& proton = out[pion.mother_id];
  check(proton.pdg_code == 2212 && proton.status == simulation_history,
        "the primary proton becomes history");
  check(proton.mothers.size() == 2 &&
            out[proton.mothers[0]].status == SHiP::aegir::hepmc::beam &&
            out[proton.mothers[1]].status == SHiP::aegir::hepmc::beam,
        "the proton keeps its generator beams");
  check(out[2].mother_id == out[0].mother_id,
        "the photon shares the muon's pion");

  // A recorded primary keeps status 1 and its generator mothers.
  check(out[1].pdg_code == -13 && out[1].mothers.size() == 1,
        "recorded primary");
  auto const& decayed = out[out[1].mother_id];
  check(
      decayed.pdg_code == 211 && decayed.status == SHiP::aegir::hepmc::decayed,
      "the generator pion keeps its status");

  // Track 7 goes through the kaon primary, whose only mother link is the
  // pre-v0.5 mother_id, to the same generator pion.
  auto const& kaon = out[out[3].mother_id];
  check(kaon.pdg_code == 321 && kaon.status == simulation_history,
        "kaon primary as history");
  check(kaon.mother_id == out[1].mother_id, "kaon's mother_id is followed");

  bool threw = false;
  try {
    SHiP::aegir::build_plane_particles({8}, find_track, primary_source,
                                       generator);
  } catch (std::logic_error const&) {
    threw = true;
  }
  check(threw, "a missing track is an error");

  if (check.failures == 0) {
    std::println("plane_particles: all checks passed");
  }
  return check.failures == 0 ? 0 : 1;
}
