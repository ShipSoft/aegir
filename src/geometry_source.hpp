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
};

}  // namespace SHiP::aegir
