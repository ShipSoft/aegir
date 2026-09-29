// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// field_map_provider.cpp — Phlex provider plugin for magnetic field maps in
// the SHiP field-map format (field_service docs/field_map_format.md). Builds a
// ship::FieldMapSource from a jsonnet `magnets` list and publishes it as a
// Job-layer product.

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "FieldService/FieldMapIO.h"
#include "FieldService/IFieldSource.h"
#include "phlex/source.hpp"
#include "provider_helpers.hpp"

PHLEX_REGISTER_PROVIDERS(s, config) {
  std::vector<ship::FieldMapSource::MagnetConfig> magnets;
  for (auto const& m :
       config.get<std::vector<phlex::configuration>>("magnets")) {
    ship::FieldMapSource::MagnetConfig magnet{
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
      std::ranges::copy(*t, magnet.translation.begin());
    }
    magnets.push_back(std::move(magnet));
  }

  // Publish as the interface type: consumers request
  // std::shared_ptr<ship::IFieldSource>.
  std::shared_ptr<ship::IFieldSource> const source =
      std::make_shared<ship::FieldMapSource>(std::move(magnets));

  aegir::provide_constant(s, "create_field", source, "field", "map", "job");
}
