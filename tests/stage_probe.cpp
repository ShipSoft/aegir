// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// stage_probe.cpp — test-only Phlex plugin
//
// Observes the generator products "mc_particles" and "event_header" from the
// stage named by the `stage` key. Phlex cannot build the graph when no
// provider carries that stage label, so a job using this plugin runs only if
// the generators label their products with the expected stage
// (scripts/generator_stage.sh).

#include <SHiP/EventHeader.hpp>
#include <SHiP/MCParticle.hpp>
#include <string>
#include <vector>

#include "phlex/module.hpp"

PHLEX_REGISTER_ALGORITHMS(m, config) {
  using namespace phlex;
  using phlex::experimental::identifier;

  identifier const stage{config.get<std::string>("stage")};

  m.observe(
       "probe_particles", [](std::vector<SHiP::MCParticle> const&) {},
       concurrency::unlimited)
      .input_family(product_selector{
          .creator = "mc_particles"_id, .layer = "event"_id, .stage = stage});

  m.observe(
       "probe_header", [](SHiP::EventHeader const&) {}, concurrency::unlimited)
      .input_family(product_selector{
          .creator = "event_header"_id, .layer = "event"_id, .stage = stage});
}
