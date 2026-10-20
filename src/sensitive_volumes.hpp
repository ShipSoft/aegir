// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// sensitive_volumes.hpp — the geometry's sensitive volumes and their
// detector ids
//
// Reads a geometry provider's `sensitive_volumes` list and resolves each
// entry's SimHit::detector_id once, at configuration time, so that a bad
// entry fails before any Geant4 object exists. Kept free of Geant4.

#pragma once

#include <SHiP/detectors/detector_id.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "phlex/configuration.hpp"

namespace SHiP::aegir {

/// One `sensitive_volumes` entry: the logical volumes whose names contain
/// `pattern` get a sensitive detector, and their hits carry `detector_id`.
struct SensitiveVolume {
  std::string pattern;
  std::int32_t detector_id;
};

/// The detector id of a bare `sensitive_volumes` pattern. The correspondence
/// between a volume-name pattern and a SHiP::detector_id is a fact about the
/// geometry, so the known patterns are listed here rather than restated in
/// every workflow.
[[nodiscard]] inline std::int32_t detector_id_for_pattern(
    std::string_view pattern) {
  auto const id = [](SHiP::detector_id d) {
    return static_cast<std::int32_t>(d);
  };
  if (pattern == "UpstreamTagger" || pattern == "upstream_tagger") {
    return id(SHiP::detector_id::UpstreamTagger);
  }
  // No SurroundTagger geometry exists yet; the test fixtures' "Scoring"/
  // "ScoringPlane" planes stand in for it.
  if (pattern == "SurroundTagger" || pattern == "Scoring" ||
      pattern == "ScoringPlane") {
    return id(SHiP::detector_id::SurroundTagger);
  }
  if (pattern == "StrawTubes" || pattern == "trackers") {
    return id(SHiP::detector_id::StrawTubes);
  }
  if (pattern == "Calorimeter" || pattern == "calorimeter") {
    return id(SHiP::detector_id::Calorimeter);
  }
  if (pattern == "TimingDetector" || pattern == "timing_detector") {
    return id(SHiP::detector_id::TimingDetector);
  }
  throw std::invalid_argument(
      "sensitive_volumes: no detector id for pattern '" + std::string{pattern} +
      "'");
}

/// Read the `sensitive_volumes` list of a geometry provider's configuration.
[[nodiscard]] inline std::vector<SensitiveVolume> read_sensitive_volumes(
    phlex::configuration const& config) {
  std::vector<SensitiveVolume> volumes;
  for (auto& pattern :
       config.get<std::vector<std::string>>("sensitive_volumes")) {
    auto const detector_id = detector_id_for_pattern(pattern);
    volumes.push_back(
        {.pattern = std::move(pattern), .detector_id = detector_id});
  }
  return volumes;
}

}  // namespace SHiP::aegir
