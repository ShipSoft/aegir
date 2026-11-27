// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// beam_spot_module.cpp — Phlex transform placing generated events in the
// beam spot
//
// Generators produce their events on the beam axis. This transform moves
// each event as a whole to where a proton of the real beam hits the target:
// it takes an mc_particles collection and returns a copy with every vertex
// shifted (and, optionally, every momentum turned to the proton's direction
// and every time offset by the proton's spill time). Shifting the event
// afterwards is exact as long as the interaction does not depend on where in
// the target's cross-section it happens, which holds for a spot well inside
// the target; protons that would miss the target are not modelled.
//
// The model follows scripts/analyse_beam_spot.py, which fits it to the beam
// files of the P42 beam-line simulation (see docs/beam_spot.md). Per event:
//
//   1. a spill time t, uniform in [0, spill_length);
//   2. the beam centre at t: a linear drift plus, with dilution, a circular
//      sweep,
//        x_c = x0 + x_drift t + R_x cos(2 pi f t + phi)
//        y_c = y0 + y_drift t + R_y sin(2 pi f t + phi);
//   3. the offset about the centre, per plane a uniform +-half_width plus a
//      Gaussian sigma (half_width 0: Gaussian; > 0: flat top with Gaussian
//      edges);
//   4. the direction: the beam at the target diverges from a point upstream,
//      so the slope is the offset over that distance, x' = x / L_x. The
//      sweep comes from its own distance (the dilution kickers), so its part
//      of the offset can have its own L.
//
// The offset applies at reference_z (the target front face by default);
// vertices elsewhere move along the proton's direction.
//
// Random numbers come from their own Philox stream keyed on the event's
// number, so the draws do not depend on thread order or on the generator.

#include <SHiP/MCParticle.hpp>
#include <SHiP/QuantityView.hpp>
#include <SHiP/Units.hpp>
#include <SHiP/random/philox_rng.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "phlex/configuration.hpp"
#include "phlex/core/product_selector.hpp"
#include "phlex/model/handle.hpp"
#include "phlex/module.hpp"
#include "seed_config.hpp"
#include "units/config_units.hpp"

namespace {

namespace su = SHiP::units;
using SHiP::aegir::get_quantity;

inline constexpr auto Hz = mp_units::si::hertz;
inline constexpr auto mm_per_s = su::mm / su::s;
using Frequency = mp_units::quantity<Hz, double>;
using Speed = mp_units::quantity<mm_per_s, double>;
using SpillTime = mp_units::quantity<su::s, double>;

// 0xBEA35907: the beam spot's stream, independent of the generators'
// (0xBEEFCAFE, 0xF14ED0A7, ...) and Geant4's (0x47345EED).
constexpr std::uint32_t kStream = 0xBEA35907;

// One transverse plane of the model.
struct Plane {
  SHiP::Length centre{};
  Speed drift{};
  SHiP::Length sweep_radius{};
  SHiP::Length half_width{};
  SHiP::Length sigma{};
  std::optional<SHiP::Length> source_distance;        // unset: parallel beam
  std::optional<SHiP::Length> sweep_source_distance;  // unset: as above
};

// An offset split into the sweep's part and the rest, which can come from
// different distances upstream.
struct Offset {
  SHiP::Length sweep;
  SHiP::Length rest;
  [[nodiscard]] SHiP::Length total() const { return sweep + rest; }
};

struct Model {
  SpillTime spill_length{1.0 * su::s};
  Frequency sweep_frequency{};
  SHiP::Angle sweep_phase{};
  Plane x, y;
  SHiP::Length reference_z{};
  bool keep_time = false;
  std::uint32_t seed = 0;
};

// Turn p by the rotation taking the z axis to the unit vector n (Rodrigues'
// formula about the axis z x n).
std::array<double, 3> rotate_z_to(std::array<double, 3> const& p,
                                  std::array<double, 3> const& n) {
  auto const [nx, ny, nz] = n;
  auto const [px, py, pz] = p;
  double const s = std::hypot(nx, ny);  // sin of the angle
  if (s == 0.0) {
    return p;
  }
  double const c = nz;
  double const kx = -ny / s;  // unit axis z x n, kz = 0
  double const ky = nx / s;
  double const kp = (kx * px) + (ky * py);  // k . p
  double const kc = kp * (1.0 - c);
  // p c + (k x p) s + k (k . p)(1 - c)
  return {(px * c) + (ky * pz * s) + (kx * kc),
          (py * c) - (kx * pz * s) + (ky * kc),
          (pz * c) + (((kx * py) - (ky * px)) * s)};
}

class BeamSpot {
 public:
  explicit BeamSpot(Model model) : m_{model} {}

  std::vector<SHiP::MCParticle> apply(
      phlex::handle<std::vector<SHiP::MCParticle>> particles) const {
    auto const event = particles.data_cell_index().number();
    SHiP::random::PhiloxRng rng{m_.seed, kStream, event};

    // Full double resolution: the spill is seconds long, the times are in ns.
    SpillTime const t =
        rng.uniform53(0.0, m_.spill_length.numerical_value_in(su::s)) * su::s;
    auto const phase =
        (2.0 * std::numbers::pi *
         (m_.sweep_frequency * t).numerical_value_in(mp_units::one)) +
        m_.sweep_phase.numerical_value_in(su::rad);

    auto const dx = offset(m_.x, t, std::cos(phase), rng);
    auto const dy = offset(m_.y, t, std::sin(phase), rng);
    double const tx = slope(m_.x, dx);
    double const ty = slope(m_.y, dy);
    double const norm = std::sqrt(1.0 + (tx * tx) + (ty * ty));
    std::array<double, 3> const direction{tx / norm, ty / norm, 1.0 / norm};
    bool const turn = tx != 0.0 || ty != 0.0;

    std::vector<SHiP::MCParticle> out{*particles};
    for (auto& p : out) {
      auto v = SHiP::view::vertex(p);
      auto const dz = v[2] - m_.reference_z;
      v[0] += dx.total() + tx * dz;
      v[1] += dy.total() + ty * dz;
      SHiP::view::set_vertex(p, v);
      if (turn) {
        p.momentum = rotate_z_to(p.momentum, direction);
      }
      if (m_.keep_time) {
        SHiP::view::set_time(p, SHiP::view::time(p) + t.in(su::ns));
      }
    }
    return out;
  }

 private:
  // The offset in one plane: centre at t plus the draw about it.
  static Offset offset(Plane const& pl, SpillTime t, double sweep,
                       SHiP::random::PhiloxRng& rng) {
    auto const hw = pl.half_width.numerical_value_in(su::mm);
    auto const sigma = pl.sigma.numerical_value_in(su::mm);
    double const spot = (hw > 0.0 ? rng.uniform(-hw, hw) : 0.0) +
                        (sigma > 0.0 ? rng.gaussian(0.0, sigma) : 0.0);
    return {.sweep = pl.sweep_radius * sweep,
            .rest = pl.centre + (pl.drift * t).in(su::mm) + spot * su::mm};
  }

  static double slope(Plane const& pl, Offset const& offset) {
    auto const ratio = [](SHiP::Length l, std::optional<SHiP::Length> d) {
      return d ? (l / *d).numerical_value_in(mp_units::one) : 0.0;
    };
    return ratio(offset.rest, pl.source_distance) +
           ratio(offset.sweep, pl.sweep_source_distance
                                   ? pl.sweep_source_distance
                                   : pl.source_distance);
  }

  Model m_;
};

phlex::configuration block(phlex::configuration const& config,
                           std::string const& key) {
  return config.get_if_present<phlex::configuration>(key).value_or(
      phlex::configuration{});
}

Model read_model(phlex::configuration const& config) {
  Model m;
  m.spill_length = get_quantity(config, "spill_length", 1.0 * su::s);
  if (m.spill_length <= SpillTime::zero()) {
    throw std::invalid_argument("beam_spot_module: spill_length must be > 0");
  }
  m.reference_z = get_quantity(config, "reference_z", 0.0 * su::mm);
  m.keep_time = config.get<bool>("keep_time", false);

  auto const centre = block(config, "centre");
  m.x.centre = get_quantity(centre, "x", 0.0 * su::mm);
  m.y.centre = get_quantity(centre, "y", 0.0 * su::mm);
  m.x.drift = get_quantity(centre, "x_drift", 0.0 * mm_per_s);
  m.y.drift = get_quantity(centre, "y_drift", 0.0 * mm_per_s);

  auto const sweep = block(config, "sweep");
  m.x.sweep_radius = get_quantity(sweep, "radius_x", 0.0 * su::mm);
  m.y.sweep_radius = get_quantity(sweep, "radius_y", 0.0 * su::mm);
  m.sweep_frequency = get_quantity(sweep, "frequency", 0.0 * Hz);
  m.sweep_phase = get_quantity(sweep, "phase", 0.0 * su::rad);

  auto const spot = block(config, "spot");
  m.x.sigma = get_quantity(spot, "sigma_x", 0.0 * su::mm);
  m.y.sigma = get_quantity(spot, "sigma_y", 0.0 * su::mm);
  m.x.half_width = get_quantity(spot, "half_width_x", 0.0 * su::mm);
  m.y.half_width = get_quantity(spot, "half_width_y", 0.0 * su::mm);
  for (auto const* pl : {&m.x, &m.y}) {
    if (pl->sigma < SHiP::Length::zero() ||
        pl->half_width < SHiP::Length::zero() ||
        pl->sweep_radius < SHiP::Length::zero()) {
      throw std::invalid_argument(
          "beam_spot_module: sigma, half_width and sweep radius must be >= 0");
    }
  }

  auto const distance =
      [](phlex::configuration const& c,
         std::string const& key) -> std::optional<SHiP::Length> {
    auto const d = c.get_if_present<double>(key);
    if (!d) {
      return std::nullopt;
    }
    if (*d <= 0.0) {
      throw std::invalid_argument(
          "beam_spot_module: source distances must be > 0 (leave them out "
          "for a parallel beam)");
    }
    return (*d * su::m).in(su::mm);
  };
  auto const source = block(config, "source_distance");
  m.x.source_distance = distance(source, "x");
  m.y.source_distance = distance(source, "y");
  m.x.sweep_source_distance = distance(sweep, "source_distance_x");
  m.y.sweep_source_distance = distance(sweep, "source_distance_y");
  m.seed = SHiP::aegir::resolve_seed(config, "beam_spot_module");
  return m;
}

}  // namespace

PHLEX_REGISTER_ALGORITHMS(m, config) {
  using namespace phlex;
  auto const from =
      config.get<std::string>("particles_from", std::string{"mc_particles"});
  auto const spot = m.make<BeamSpot>(read_model(config));
  spot.transform("smear", &BeamSpot::apply, concurrency::unlimited)
      .input_family(product_selector{.creator = experimental::identifier{from},
                                     .layer = "event"_id})
      .output_product_suffixes("particles");
}
