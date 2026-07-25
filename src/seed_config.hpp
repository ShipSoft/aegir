// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// seed_config.hpp — shared resolution of the optional `seed` config key
//
// Reproducibility is opt-in: without a configured seed, draw one at random
// so independent jobs produce independent output, and log it so any run can
// still be reproduced after the fact. Parsed as int64 so the full uint32
// range survives — drawn seeds are logged as uint32 values, and pasting one
// back must round-trip.

#pragma once

#include <spdlog/spdlog.h>

#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

#include "phlex/configuration.hpp"

namespace aegir {

[[nodiscard]] inline std::uint32_t resolve_seed(
    phlex::configuration const& config, std::string const& component) {
  auto const configured_seed = config.get_if_present<std::int64_t>("seed");
  if (configured_seed &&
      (*configured_seed < 0 ||
       *configured_seed > std::numeric_limits<std::uint32_t>::max()))
    throw std::runtime_error(component + ": seed " +
                             std::to_string(*configured_seed) +
                             " is outside the valid range [0, 4294967295]");
  auto const seed = configured_seed
                        ? static_cast<std::uint32_t>(*configured_seed)
                        : static_cast<std::uint32_t>(std::random_device{}());
  if (!configured_seed)
    spdlog::info(
        "{}: no seed configured — drew random seed {}; set 'seed: {}' to "
        "reproduce this run",
        component, seed, seed);
  return seed;
}

}  // namespace aegir
