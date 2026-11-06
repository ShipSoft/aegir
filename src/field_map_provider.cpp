// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// field_map_provider.cpp — Phlex provider plugin for magnetic field maps in
// the SHiP field-map format (field_service docs/field_map_format.md). Builds a
// SHiP::FieldMapSource and publishes it as a Job-layer product. The magnets
// come either from a placement manifest (`placements`, field_service
// docs/placements_format.md) or from a jsonnet `magnets` list.

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "SHiP/FieldService/FieldMapIO.h"
#include "SHiP/FieldService/IFieldSource.h"
#include "phlex/source.hpp"
#include "provider_helpers.hpp"

namespace {

std::vector<SHiP::FieldMapSource::MagnetConfig> magnetsFromConfig(
    std::vector<phlex::configuration> const& entries) {
  std::vector<SHiP::FieldMapSource::MagnetConfig> magnets;
  for (auto const& m : entries) {
    SHiP::FieldMapSource::MagnetConfig magnet{
        .name = m.get<std::string>("name"),
        .volume_pattern = m.get<std::string>("volume_pattern"),
        .file = m.get<std::string>("file"),
        .map = m.get<std::string>("map"),
    };
    // Maps are stored in their own frame; `translation` (mm) places the
    // map's origin in the global frame. Omitted means the origins coincide.
    if (auto const t = m.get_if_present<std::vector<double>>("translation")) {
      if (t->size() != magnet.translation.size()) {
        throw std::invalid_argument("field_map_provider: magnet '" +
                                    magnet.name +
                                    "': translation needs 3 values (mm)");
      }
      if (!std::ranges::all_of(*t, [](double v) { return std::isfinite(v); })) {
        throw std::invalid_argument("field_map_provider: magnet '" +
                                    magnet.name +
                                    "': translation values must be finite");
      }
      std::ranges::copy(*t, magnet.translation.begin());
    }
    magnets.push_back(std::move(magnet));
  }
  return magnets;
}

}  // namespace

PHLEX_REGISTER_PROVIDERS(s, config) {
  auto const placements = config.get_if_present<std::string>("placements");
  auto const entries =
      config.get_if_present<std::vector<phlex::configuration>>("magnets");
  if (placements.has_value() == entries.has_value()) {
    throw std::invalid_argument(
        "field_map_provider: set exactly one of 'placements' (a placement "
        "manifest) and 'magnets' (a list of magnets)");
  }
  // A bare manifest name resolves via $SHIPFIELD_ROOT/share/field/, where
  // the ship-field-maps package installs placements.json and its maps.
  auto magnets = placements ? SHiP::readFieldMapPlacements(*placements).magnets
                            : magnetsFromConfig(*entries);

  // Publish as the interface type: consumers request
  // std::shared_ptr<SHiP::IFieldSource>.
  std::shared_ptr<SHiP::IFieldSource> const source =
      std::make_shared<SHiP::FieldMapSource>(std::move(magnets));

  SHiP::aegir::provide_constant(s, "create_field", source, "field", "map",
                                "job");
}
