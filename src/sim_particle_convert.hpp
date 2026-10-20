// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// sim_particle_convert.hpp — SimParticle → MCParticle projection

#pragma once

#include <SHiP/MCParticle.hpp>
#include <SHiP/QuantityView.hpp>
#include <SHiP/SimParticle.hpp>
#include <SHiP/Units.hpp>

#include "hepmc_status.hpp"
#include "math_utils.hpp"

namespace SHiP::aegir {

// Project a simulation SimParticle onto an MCParticle at its production point,
// with status final_state and no mothers; mother links are the caller's, since
// SimParticle::parent_id is a track id, not an index into the output vector.
// SimParticle::energy is kinetic while MCParticle::energy is total; recover the
// total energy from |p|c and the kinetic energy without needing a PDG mass
// table: E_total = ((|p|c)^2 + E_kin^2) / (2 E_kin), from
// mc^2 = ((|p|c)^2 - E_kin^2) / (2 E_kin) and E_total = E_kin + mc^2.
inline SHiP::MCParticle to_mc_particle(SHiP::SimParticle const& sp) {
  namespace su = SHiP::units;
  SHiP::MCParticle mc;
  mc.pdg_code = sp.pdg_code;
  // Same storage units on both sides — direct copies, no unit crossing.
  mc.vertex = sp.vertex;
  mc.momentum = sp.momentum;
  auto const pc = (SHiP::aegir::magnitude(SHiP::view::momentum(sp)) * su::c)
                      .in(su::GeV);           // |p|c; conversion factor is 1
  auto const e_kin = SHiP::view::energy(sp);  // SimParticle::energy is kinetic
  SHiP::view::set_energy(mc, e_kin > SHiP::Energy::zero()
                                 ? (pc * pc + e_kin * e_kin) / (2.0 * e_kin)
                                 : pc);
  mc.time = sp.time;
  mc.status = hepmc::final_state;
  return mc;
}

}  // namespace SHiP::aegir
