// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// eventcalc_source.cpp — Phlex source plugin reading EventCalc-SHiP records
//
// Provides MCParticle vectors from an EventCalc `<LLP>_..._data.dat` file: a
// plain-text record of sampled LLP decays inside the SHiP decay volume, with
// the LLP kinematics, the decay vertex and the metastable decay products.
// Reading it needs no EventCalc installation, so LLP phenomenology stays
// decoupled from the aegir build; see docs/eventcalc.md for how to produce
// the input files.
//
// The LLP itself is not emitted: Geant4 has no particle definition for an HNL
// or a dark scalar, and the decay has already happened upstream. What is
// tracked are the daughters, all originating at the decay vertex.
//
// EventCalc rows are not equally probable — each carries the probability that
// its LLP decays inside the volume — so the source also publishes an
// EventHeader whose weight is that probability. Any distribution built from
// this sample must be weighted by it.

#include <SHiP/EventHeader.hpp>
#include <SHiP/MCParticle.hpp>
#include <SHiP/QuantityView.hpp>
#include <SHiP/Units.hpp>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "eventcalc_reader.hpp"
#include "hepmc_status.hpp"
#include "mc_particle_source.hpp"
#include "units/config_units.hpp"

namespace {

namespace su = SHiP::units;

bool is_neutrino(std::int32_t pdg) {
  auto const a = std::abs(pdg);
  return a == 12 || a == 14 || a == 16;
}

class EventCalcSource : public phlex::source {
 public:
  EventCalcSource(std::string stage, std::string const& file,
                  long long first_entry, bool skip_neutrinos,
                  SHiP::Length offset_x, SHiP::Length offset_y,
                  SHiP::Length offset_z)
      : stage_{std::move(stage)},
        reader_{file},
        first_entry_{first_entry},
        skip_neutrinos_{skip_neutrinos},
        offset_{offset_x, offset_y, offset_z} {}

  std::vector<SHiP::MCParticle> generate(phlex::data_cell_index const& id) {
    auto const& event = reader_.at(entry_for(id));

    // The LLP is produced at the target, which is the EventCalc origin, so
    // the flight path is measured from there — before the geometry offset
    // is applied, or a non-zero offset would corrupt the timing.
    auto const time =
        SHiP::aegir::eventcalc::flight_time(event.vertex, event.llp);

    // The offset shifts into the frame of the configured geometry provider,
    // should the two origins differ.
    SHiP::Vec3<SHiP::Length> const vertex{event.vertex[0] + offset_[0],
                                          event.vertex[1] + offset_[1],
                                          event.vertex[2] + offset_[2]};

    // An event may come out empty: every daughter a neutrino with
    // skip_neutrinos set (e.g. N -> nu nu nu), or a row whose product groups
    // are all padding. It is still emitted, never skipped: events must stay
    // one-to-one with input rows for original_event_id to hold, and the
    // weight of an invisible decay still belongs in the normalisation.
    std::vector<SHiP::MCParticle> particles;
    particles.reserve(event.daughters.size());

    for (auto const& d : event.daughters) {
      if (skip_neutrinos_ && is_neutrino(d.pdg)) continue;
      SHiP::MCParticle mc;
      mc.pdg_code = d.pdg;
      SHiP::view::set_momentum(mc, d.momentum);
      SHiP::view::set_energy(mc, d.energy);
      SHiP::view::set_vertex(mc, vertex);
      SHiP::view::set_time(mc, time);
      mc.mother_id = -1;
      mc.status = SHiP::aegir::hepmc::final_state;
      particles.push_back(mc);
    }
    return particles;
  }

  // The weight is P_decay,LLP — the probability that this LLP decays inside
  // the volume — and it varies row to row, so it belongs on the event rather
  // than on each particle. original_event_id records which row of the input
  // this event came from, so a simulated event can be traced back to the
  // EventCalc record even after splitting a file across jobs.
  SHiP::EventHeader header(phlex::data_cell_index const& id) {
    auto const entry = entry_for(id);
    return SHiP::EventHeader{reader_.at(entry).decay_probability,
                             static_cast<std::int64_t>(entry)};
  }

  phlex::provider_bundles create_providers(
      phlex::product_selector const& selector) override {
    // The reader is immutable after construction, so unlike the GENIE source
    // (whose TTree forces serial access) events can be served concurrently.
    return SHiP::aegir::mc_particle_provider_bundles(
        selector, stage_,
        [this](phlex::data_cell_index const& id) { return generate(id); },
        phlex::concurrency::unlimited,
        [this](phlex::data_cell_index const& id) { return header(id); });
  }

  phlex::index_generator indices() override { co_return; }

 private:
  std::size_t entry_for(phlex::data_cell_index const& id) const {
    auto const entry = first_entry_ + static_cast<long long>(id.number());
    if (entry < 0 || static_cast<std::size_t>(entry) >= reader_.size())
      throw std::runtime_error(
          "eventcalc_source: input exhausted — the workflow requested event " +
          std::to_string(entry) + " but '" + reader_.file_name() +
          "' holds only " + std::to_string(reader_.size()) +
          " decays. Reduce the driver's event count (see "
          "scripts/count_eventcalc_events.py) or provide a larger file.");
    return static_cast<std::size_t>(entry);
  }

  std::string stage_;
  SHiP::aegir::eventcalc::Reader reader_;
  long long first_entry_;
  bool skip_neutrinos_;
  SHiP::Vec3<SHiP::Length> offset_;
};

}  // namespace

PHLEX_REGISTER_SOURCE(s, config) {
  auto file = config.get<std::string>("file");
  auto first_entry = config.get<long>("first_entry", 0L);
  if (first_entry < 0)
    throw std::runtime_error("eventcalc_source: first_entry must be >= 0");

  // Neutrinos are recorded by EventCalc but invisible to the detector;
  // tracking them costs time and yields nothing.
  auto skip_neutrinos = config.get<bool>("skip_neutrinos", true);

  auto offset_x = SHiP::aegir::get_quantity(config, "offset_x", 0.0 * su::mm);
  auto offset_y = SHiP::aegir::get_quantity(config, "offset_y", 0.0 * su::mm);
  auto offset_z = SHiP::aegir::get_quantity(config, "offset_z", 0.0 * su::mm);
  auto stage = SHiP::aegir::source_stage(config, "eventcalc");

  s.add_source<EventCalcSource>("eventcalc", std::move(stage), file,
                                static_cast<long long>(first_entry),
                                skip_neutrinos, offset_x, offset_y, offset_z);
}
