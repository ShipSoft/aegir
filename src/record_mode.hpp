// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// record_mode.hpp — how much of a generator's event record to emit
//
// Shared by the generator sources so the `record` option means the same thing
// everywhere. Free of Phlex and data-model dependencies so the header-only
// Pythia8 helpers (and the standalone benchmark that shares them) can use it.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aegir {

enum class record_mode : std::uint8_t {
  final_state,  ///< Only particles the generator left undecayed
  full,         ///< The whole record, statuses and mother links intact
};

/// Parse the `record` configuration option. Throws on an unknown value rather
/// than silently falling back: a typo here would quietly change the physics
/// content of the output.
[[nodiscard]] inline record_mode parse_record_mode(std::string_view value,
                                                   std::string_view source) {
  if (value == "final_state") {
    return record_mode::final_state;
  }
  if (value == "full") {
    return record_mode::full;
  }
  throw std::runtime_error(std::string{source} + ": unknown record mode '" +
                           std::string{value} +
                           "' (expected 'final_state' or 'full')");
}

}  // namespace aegir
