// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// field_resource.hpp — The magnetic field as a phlex resource
//
// The field_*_provider plugins register one; algorithms declare
// phlex::resource<FieldResource>{} and receive a FieldResource const*.

#pragma once

#include <memory>
#include <utility>

#include "SHiP/FieldService/IFieldSource.h"

namespace SHiP::aegir {

/// Phlex keys resources by their concrete type, so each field_*_provider
/// plugin registers this wrapper around its own SHiP::IFieldSource.
struct FieldResource {
  explicit FieldResource(std::unique_ptr<SHiP::IFieldSource const> s)
      : source{std::move(s)} {}

  std::unique_ptr<SHiP::IFieldSource const> source;
};

}  // namespace SHiP::aegir
