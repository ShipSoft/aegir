// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// field_null_provider.cpp — Phlex resource plugin for workflows that don't
// need a magnetic field. Registers an empty IFieldSource (no regions);
// ConstructSDandField() then installs no G4FieldManager and Geant4 propagates
// in vacuum throughout.

#include <memory>
#include <vector>

#include "SHiP/FieldService/IFieldSource.h"
#include "field_resource.hpp"
#include "phlex/resource.hpp"

namespace {

class NullFieldSource : public SHiP::IFieldSource {
  std::vector<SHiP::FieldRegion> empty_;

 public:
  [[nodiscard]] std::vector<SHiP::FieldRegion> const& regions() const override {
    return empty_;
  }
};

}  // namespace

PHLEX_REGISTER_RESOURCES(r) {
  r.add_unlimited_resource<SHiP::aegir::FieldResource>(
      std::make_unique<NullFieldSource>());
}
