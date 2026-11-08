// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_source.hpp — Interface for pluggable geometry sources
//
// Implementations provide the G4 world volume and declare which logical
// volumes should be instrumented with sensitive detectors.
//
// This type is used as a phlex data product (Job-layer Provider).

#pragma once

#include <G4VPhysicalVolume.hh>
#include <optional>
#include <vector>

#include "sensitive_volumes.hpp"

namespace SHiP::aegir {

class IGeometrySource {
 public:
  virtual ~IGeometrySource() = default;

  /// Build or return the cached G4 world volume.
  /// Must be called only from the master thread, inside
  /// G4VUserDetectorConstruction::Construct().
  /// Idempotent: subsequent calls return the same pointer.
  [[nodiscard]] virtual G4VPhysicalVolume* construct() const = 0;

  /// The volumes to assign sensitive detectors to, with the detector id of
  /// their hits. Patterns are matched as substrings against
  /// G4LogicalVolumeStore entries; the first matching entry wins.
  [[nodiscard]] virtual std::vector<SensitiveVolume> const& sensitiveVolumes()
      const = 0;

  /// The copy number this geometry gives a volume that carries no id, if it
  /// has one: a GeoModel geometry's conversion gives an untagged volume
  /// g4::kGeoModelUntaggedCopyNumber. Hits whose id level has it get
  /// g4::kNoGeometryNodeId. In other geometries every copy number is an id.
  [[nodiscard]] virtual std::optional<int> untaggedCopyNumber() const {
    return std::nullopt;
  }
};

}  // namespace SHiP::aegir
