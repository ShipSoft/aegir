// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// particle_gun_source.cpp — Phlex source plugin
//
// Provides MCParticle vectors from a configurable particle gun.
// Each event generates a single particle with fixed or randomised kinematics.

#include <SHiP/MCParticle.hpp>
#include <SHiP/QuantityView.hpp>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "mc_particle_source.hpp"
#include "philox_rng.hpp"
#include "seed_config.hpp"
#include "units/config_units.hpp"

namespace {

namespace su = ship::units;

class ParticleGun : public phlex::source {
 public:
  ParticleGun(int pdg, ship::Momentum p_min, ship::Momentum p_max,
              ship::Angle max_theta, ship::Vec3<ship::Length> vertex,
              std::uint32_t seed)
      : pdg_{pdg},
        p_min_{p_min},
        p_max_{p_max},
        max_theta_{max_theta},
        vertex_{vertex},
        seed_{seed} {}

  std::vector<SHiP::MCParticle> generate(phlex::data_cell_index const& id) {
    auto event_number = static_cast<std::uint32_t>(id.number());
    // 0xBEEFCAFE: the gun's stream, independent of fixed_target (0xF14ED0A7)
    // and geant4 (0x47345EED); the event number selects the counter
    // sub-stream so events stay decorrelated for any base seed.
    aegir::PhiloxRng rng{seed_, 0xBEEFCAFE, event_number};

    // Sampling stays on raw numbers so the Philox stream is bit-identical;
    // the bounds are unwrapped in the canonical units on the same lines.
    double p = rng.uniform(p_min_.numerical_value_in(su::GeV_per_c),
                           p_max_.numerical_value_in(su::GeV_per_c));
    double theta = rng.uniform(0.0, max_theta_.numerical_value_in(su::rad));
    double phi = rng.uniform(0.0, 2.0 * std::numbers::pi);

    SHiP::MCParticle mc;
    mc.pdgCode = pdg_;
    ship::view::setVertex(mc, vertex_);
    ship::view::setMomentum(
        mc, ship::vecOf<ship::Momentum>({p * std::sin(theta) * std::cos(phi),
                                         p * std::sin(theta) * std::sin(phi),
                                         p * std::cos(theta)}));
    // Assume massless, E = |p|c (good enough for muons at high p).
    ship::view::setEnergy(mc, (p * su::GeV_per_c * su::c).in(su::GeV));
    ship::view::setTime(mc, ship::Time::zero());
    mc.motherId = -1;
    mc.status = 1;

    return {mc};
  }

  phlex::detail::provider_bundles create_providers(
      phlex::product_selector const& selector) override {
    return aegir::mc_particle_provider_bundles(
        selector,
        [this](phlex::data_cell_index const& id) { return generate(id); },
        phlex::concurrency::unlimited);
  }

  phlex::index_generator indices() override { co_return; }

 private:
  int pdg_;
  ship::Momentum p_min_, p_max_;
  ship::Angle max_theta_;
  ship::Vec3<ship::Length> vertex_;
  std::uint32_t seed_;
};

}  // namespace

PHLEX_REGISTER_SOURCE(s, config) {
  using namespace phlex;

  auto pdg = config.get<int>("pdg", 13);  // muon
  auto p_min = aegir::get_quantity(config, "p_min", 10.0 * su::GeV_per_c);
  auto p_max = aegir::get_quantity(config, "p_max", 100.0 * su::GeV_per_c);
  auto max_theta = aegir::get_quantity(config, "max_theta", 0.1 * su::rad);
  auto vx = aegir::get_quantity(config, "vertex_x", 0.0 * su::mm);
  auto vy = aegir::get_quantity(config, "vertex_y", 0.0 * su::mm);
  // Default: upstream of the target.
  auto vz = aegir::get_quantity(config, "vertex_z", -500.0 * su::mm);
  auto seed = aegir::resolve_seed(config, "particle_gun");

  s.add_source<ParticleGun>("particle_gun", pdg, p_min, p_max, max_theta,
                            ship::Vec3<ship::Length>{vx, vy, vz}, seed);
}
