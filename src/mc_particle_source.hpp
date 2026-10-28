// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// mc_particle_source.hpp — shared helper for event-generator plugins
//
// Every aegir event generator publishes the same products: the
// "mc_particles" collection (std::vector<SHiP::MCParticle>) and the
// "event_header" record (SHiP::EventHeader), both on the "event" layer.
// provide_mc_particles registers both as explicit providers from a
// PHLEX_REGISTER_PROVIDERS plugin. Phlex labels explicit providers with the
// job's stage (a `phlex --stage` override included), so the generators need
// no stage setting of their own.
//
// The header carries the per-event weight. Generators that sample uniformly
// leave the header generator empty and get the unweighted default (weight
// 1.0, no originating id), which keeps the output schema identical across all
// workflows; generators reading a weighted record pass a header generator.
//
// The two products are separate phlex nodes and may run concurrently for the
// same event. A generator whose two functions share a non-thread-safe backend
// (a file reader, say) must lock it.
//
// mc_particle_provider_bundles and source_stage remain for generators written
// as phlex::source subclasses (PHLEX_REGISTER_SOURCE), such as plugins built
// outside aegir. Phlex does not pass the job's stage to sources, so those
// read it from a `stage` key in their own configuration block, and Phlex
// rejects the reserved "CURRENT" there.

#pragma once

#include <SHiP/EventHeader.hpp>
#include <SHiP/MCParticle.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "phlex/concurrency.hpp"
#include "phlex/configuration.hpp"
#include "phlex/core/product_selector.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/model/products.hpp"
#include "phlex/source.hpp"

namespace SHiP::aegir {

// Generator signature: produce the particles for a single data cell (event).
using mc_particle_generator =
    std::function<std::vector<SHiP::MCParticle>(phlex::data_cell_index const&)>;

// Generator signature: produce the metadata for a single data cell. May be
// empty, in which case the unweighted default is published.
using event_header_generator =
    std::function<SHiP::EventHeader(phlex::data_cell_index const&)>;

// Register the "mc_particles" and "event_header" providers on the "event"
// layer. `s` is the proxy a PHLEX_REGISTER_PROVIDERS plugin receives. The
// generators are invoked once per data cell.
template <typename Registrar>
void provide_mc_particles(Registrar& s, mc_particle_generator generate,
                          phlex::concurrency max_concurrency,
                          event_header_generator generate_header = {}) {
  s.provide(
       "generate_particles",
       [generate = std::move(generate)](phlex::data_cell_index const& id)
           -> std::vector<SHiP::MCParticle> { return generate(id); },
       max_concurrency)
      .output_product("mc_particles", "particles", "event");

  s.provide(
       "generate_header",
       [generate_header = std::move(generate_header)](
           phlex::data_cell_index const& id) -> SHiP::EventHeader {
         // Unweighted default: every event counts once, no provenance.
         if (!generate_header) {
           return SHiP::EventHeader{};
         }
         return generate_header(id);
       },
       max_concurrency)
      .output_product("event_header", "header", "event");
}

// For phlex::source generators: read the required `stage` key of the
// source's configuration block.
[[nodiscard]] inline std::string source_stage(
    phlex::configuration const& config, std::string const& component) {
  auto stage = config.get_if_present<std::string>("stage");
  if (!stage || stage->empty() || *stage == "CURRENT") {
    throw std::runtime_error(
        component +
        ": the source block needs a 'stage' key naming the job's stage "
        "(e.g. stage: 'simulation'); 'CURRENT' is reserved");
  }
  return std::move(*stage);
}

// Build the implicit-provider bundle(s) for a source emitting the
// "mc_particles" and "event_header" products in the given stage, honouring
// the framework's product selector. The generators are invoked once per data
// cell and their results type-erased into Phlex products.
inline phlex::provider_bundles mc_particle_provider_bundles(
    phlex::product_selector const& selector, std::string const& stage,
    mc_particle_generator generate, phlex::concurrency max_concurrency,
    event_header_generator generate_header = {}) {
  using namespace phlex::experimental;

  phlex::provider_bundles bundles;
  std::string const layer = "event";

  product_specification particles_spec{
      algorithm_name::create("mc_particles"), identifier{"particles"},
      make_type_id<std::vector<SHiP::MCParticle>>()};

  if (selector.match(particles_spec, identifier{layer}, identifier{stage})) {
    bundles.push_back(phlex::provider_bundle{
        .provider_function =
            [generate = std::move(generate)](phlex::data_cell_index const& id)
            -> product_ptr { return product_for(generate(id)); },
        .max_concurrency = max_concurrency,
        .spec = std::move(particles_spec),
        .layer = layer,
        .stage = stage});
  }

  product_specification header_spec{algorithm_name::create("event_header"),
                                    identifier{"header"},
                                    make_type_id<SHiP::EventHeader>()};

  if (selector.match(header_spec, identifier{layer}, identifier{stage})) {
    bundles.push_back(phlex::provider_bundle{
        .provider_function =
            [generate_header = std::move(generate_header)](
                phlex::data_cell_index const& id) -> product_ptr {
          // Unweighted default: every event counts once, no provenance.
          if (!generate_header) {
            return product_for(SHiP::EventHeader{});
          }
          return product_for(generate_header(id));
        },
        .max_concurrency = max_concurrency,
        .spec = std::move(header_spec),
        .layer = layer,
        .stage = stage});
  }

  return bundles;
}

}  // namespace SHiP::aegir
