// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// record_mode_test.cpp — parsing the `record` option, and value_or_throw

#include "record_mode.hpp"

#include <expected>
#include <print>
#include <stdexcept>
#include <string>

#include "expected_util.hpp"

namespace {

// Counts and reports failed checks.
struct Checker {
  int failures = 0;

  void operator()(bool ok, std::string const& what) {
    if (!ok) {
      std::println(stderr, "FAILED: {}", what);
      ++failures;
    }
  }
};

}  // namespace

int main() {
  using SHiP::aegir::parse_record_mode;
  using SHiP::aegir::record_mode;
  using SHiP::aegir::value_or_throw;
  Checker check;

  check(parse_record_mode("final_state") == record_mode::final_state,
        "final_state");
  check(parse_record_mode("full") == record_mode::full, "full");
  auto const typo = parse_record_mode("ful");
  check(!typo && typo.error() ==
                     "unknown record mode 'ful' (expected 'final_state' or "
                     "'full')",
        "an unknown mode is an error");

  check(value_or_throw(parse_record_mode("full"), "src") == record_mode::full,
        "value_or_throw passes a value through");

  std::string message;
  try {
    (void)value_or_throw(parse_record_mode("ful"), "pythia8");
  } catch (std::runtime_error const& e) {
    message = e.what();
  }
  check(message.starts_with("pythia8: unknown record mode 'ful'"),
        "value_or_throw prefixes the context: " + message);

  bool threw_invalid = false;
  try {
    value_or_throw<std::invalid_argument>(
        std::expected<void, std::string>{std::unexpected{"bad"}}, "ctx");
  } catch (std::invalid_argument const& e) {
    threw_invalid = std::string{e.what()} == "ctx: bad";
  }
  check(threw_invalid, "value_or_throw<void> throws the requested type");
  value_or_throw(std::expected<void, std::string>{}, "ctx");

  return check.failures == 0 ? 0 : 1;
}
