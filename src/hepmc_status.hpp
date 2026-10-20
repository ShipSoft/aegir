// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// hepmc_status.hpp — HepMC generator status codes
//
// SHiP::MCParticle::status carries the HepMC status code (the conventions
// agreed in February 2009), as produced by Pythia8's Particle::statusHepMC()
// and used by EDM4hep for its generatorStatus. Geant4's own HepMC example
// uses the same status == 1 predicate to decide what to track.
//
// Deliberately free of includes so the Pythia8 benchmark's stand-in particle
// struct can use it without pulling in the data model.

#pragma once

namespace SHiP::aegir::hepmc {

inline constexpr int empty = 0;          ///< Empty entry — never tracked
inline constexpr int final_state = 1;    ///< Not decayed by the generator
inline constexpr int decayed = 2;        ///< Decayed SM hadron, tau or muon
inline constexpr int documentation = 3;  ///< Documentation entry
inline constexpr int beam = 4;           ///< Incoming beam particle
// 11-200 are intermediate entries with a generator-dependent classification.
inline constexpr int intermediate_min = 11;
inline constexpr int intermediate_max = 200;
// 201 and above are left to the simulation. aegir uses 201 for the Geant4
// tracks in the ancestry of a particle recorded for replay (plane_particles).
inline constexpr int simulation_history = 201;

/// True for entries the generator left undecayed — the ones a detector
/// simulation is responsible for. Note that "final state" does not mean
/// stable: a final-state K_S is status 1 and is expected to decay in the
/// detector. Everything else is either the generator's bookkeeping (0, 3, 4,
/// 11-200) or already handled by the generator (2), and tracking it would
/// double-count the event.
[[nodiscard]] constexpr bool is_final_state(int status) noexcept {
  return status == final_state;
}

/// True for aegir's own history entries (status 201): written by an earlier
/// stage, expected in its output and never tracked. Other codes above 200
/// are not aegir's, so they are reported like any other skipped entry.
[[nodiscard]] constexpr bool is_simulation_history(int status) noexcept {
  return status == simulation_history;
}

static_assert(is_final_state(final_state));
static_assert(!is_final_state(simulation_history));
static_assert(is_simulation_history(simulation_history));
static_assert(!is_simulation_history(intermediate_max));
static_assert(!is_simulation_history(simulation_history + 1));
static_assert(!is_final_state(empty));
static_assert(!is_final_state(decayed));
static_assert(!is_final_state(documentation));
static_assert(!is_final_state(beam));
static_assert(!is_final_state(intermediate_min));

}  // namespace SHiP::aegir::hepmc
