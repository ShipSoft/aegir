// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// checker.hpp — counts and reports the failed checks of a test program

#pragma once

#include <cstdio>
#include <print>
#include <string_view>

namespace SHiP::aegir::test {

/// Counts and reports failed checks: `check(ok, what)` prints
/// "FAILED: <what>" to stderr when `ok` is false. A test program returns
/// non-zero when `failures` is.
struct Checker {
  int failures = 0;

  void operator()(bool ok, std::string_view what) {
    if (!ok) {
      std::println(stderr, "FAILED: {}", what);
      ++failures;
    }
  }
};

}  // namespace SHiP::aegir::test
