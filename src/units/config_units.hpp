// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// config_units.hpp — typed reads of dimensional configuration parameters
//
// Workflow configuration stays plain numbers (jsonnet has no unit syntax);
// the C++ parameter definition is the single source of truth for the unit.
// From the get_quantity() call onward the value is a typed quantity and can
// no longer be mixed up with a value in another unit.

#pragma once

#include <SHiP/Units.hpp>
#include <string>

#include "phlex/configuration.hpp"

namespace aegir {

/// Read config key `key` as a quantity in the unit of `fallback`, e.g.
///   auto beam_energy = get_quantity(config, "beam_energy", 400.0 * su::GeV);
template <auto U>
[[nodiscard]] mp_units::quantity<U, double> get_quantity(
    phlex::configuration const& config, std::string const& key,
    mp_units::quantity<U, double> fallback) {
  return config.get<double>(key, fallback.numerical_value_in(U)) * U;
}

}  // namespace aegir
