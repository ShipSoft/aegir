// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// expected_util.hpp — turn a std::expected error into an exception
//
// Parse, validate and lookup helpers return std::expected<T, std::string>, so
// a caller can test or recover from a failure without try/catch. The places
// phlex calls into (source, provider and module constructors) still report
// errors as exceptions; value_or_throw() is where one becomes the other.

#pragma once

#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace SHiP::aegir {

/// The value of `result`, or an `Exception` reading "<context>: <error>".
template <typename Exception = std::runtime_error, typename T>
T value_or_throw(std::expected<T, std::string> result,
                 std::string_view context) {
  if (!result) {
    throw Exception(std::string{context} + ": " + result.error());
  }
  if constexpr (!std::is_void_v<T>) {
    return *std::move(result);
  }
}

}  // namespace SHiP::aegir
