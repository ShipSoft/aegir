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
#include <expected>
#include <string>
#include <string_view>

namespace SHiP::aegir {

enum class record_mode : std::uint8_t {
  final_state,  ///< Only particles the generator left undecayed
  full,         ///< The whole record, statuses and mother links intact
};

/// Parse the `record` configuration option. An unknown value is an error
/// rather than a silent fallback: a typo here would quietly change the physics
/// content of the output.
[[nodiscard]] inline std::expected<record_mode, std::string> parse_record_mode(
    std::string_view value) {
  if (value == "final_state") {
    return record_mode::final_state;
  }
  if (value == "full") {
    return record_mode::full;
  }
  return std::unexpected{"unknown record mode '" + std::string{value} +
                         "' (expected 'final_state' or 'full')"};
}

}  // namespace SHiP::aegir
