// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// math_utils.hpp — small numeric helpers shared across plugins

#pragma once

#include <SHiP/Units.hpp>
#include <array>
#include <cmath>

namespace aegir {

// Euclidean norm of a raw 3-vector already in storage units (momentum,
// position, ...) — e.g. straight off a persisted struct.
inline double magnitude(std::array<double, 3> const& v) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Euclidean norm of a quantity 3-vector. Delegates to the double overload on
// the bitwise-unwrapped components, so wrapping never changes the result.
template <typename Q>
[[nodiscard]] inline Q magnitude(ship::Vec3<Q> const& v) {
  return ship::quantityOf<Q>(magnitude(ship::raw(v)));
}

}  // namespace aegir
