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
#include <SHiP/detectors/detector_id.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace SHiP {

class IGeometrySource {
 public:
  virtual ~IGeometrySource() = default;

  /// Build or return the cached G4 world volume.
  /// Must be called only from the master thread, inside
  /// G4VUserDetectorConstruction::Construct().
  /// Idempotent: subsequent calls return the same pointer.
  [[nodiscard]] virtual G4VPhysicalVolume* construct() const = 0;

  /// Volume names to assign sensitive detectors to.
  /// Matched as substrings against G4LogicalVolumeStore entries.
  [[nodiscard]] virtual std::vector<std::string> const& sensitiveVolumes()
      const = 0;
};

/// Maps a sensitive-volume config pattern (e.g. "trackers") to the canonical
/// detector it belongs to. Hardcoded in aegir rather than driven by config:
/// the correspondence between a G4 volume-name pattern and a
/// SHiP::detector_id is a fact about the geometry, not something a workflow
/// should restate per-config.
[[nodiscard]] inline detector_id detector_id_for_pattern(
    std::string const& pattern) {
  if (pattern == "UpstreamTagger") return detector_id::UpstreamTagger;
  // No SurroundTagger geometry exists yet; the test fixtures' "Scoring"/
  // "ScoringPlane" planes stand in for it.
  if (pattern == "SurroundTagger" || pattern == "Scoring" ||
      pattern == "ScoringPlane")
    return detector_id::SurroundTagger;
  if (pattern == "trackers" || pattern == "StrawTubes")
    return detector_id::StrawTubes;
  if (pattern == "Calorimeter") return detector_id::Calorimeter;
  if (pattern == "timing_detector" || pattern == "TimingDetector")
    return detector_id::TimingDetector;
  throw std::runtime_error(
      "No detector_id mapping for sensitive-volume "
      "pattern '" +
      pattern + "'");
}

}  // namespace SHiP
