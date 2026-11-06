// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_geomodel_provider.cpp — Phlex provider plugin for GeoModel geometry
//
// Loads a GeoModel .db file via SHiP::GeometryService and converts to Geant4.

#include <G4LogicalVolume.hh>
#include <G4PVPlacement.hh>
#include <cstdlib>
#include <filesystem>  // NOLINT(build/c++17)
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "SHiP/GeometryService/GeometryService.h"
#include "geometry_source.hpp"
#include "phlex/source.hpp"
#include "provider_helpers.hpp"

namespace {

class GeoModelGeometrySource : public SHiP::aegir::IGeometrySource {
  std::string db_path_;
  std::vector<SHiP::aegir::SensitiveVolume> sensitive_vols_;
  mutable std::once_flag init_flag_;
  mutable std::shared_ptr<SHiP::GeometryService> service_;
  mutable G4VPhysicalVolume* world_ = nullptr;

 public:
  GeoModelGeometrySource(std::string path,
                         std::vector<SHiP::aegir::SensitiveVolume> sv)
      : db_path_{std::move(path)}, sensitive_vols_{std::move(sv)} {}

  [[nodiscard]] G4VPhysicalVolume* construct() const override {
    std::call_once(init_flag_, [this] {
      // The registry shares one service — and with it one GeoModel->G4
      // conversion — with every other user of the same file in this
      // process; in the full GENIE chain, aegir-genie's geometry analyzer
      // has already loaded and converted it (aegir-genie issue #11).
      service_ = SHiP::GeometryService::sharedFromFile(db_path_);
      // Throws if the conversion fails, so the volume is never null.
      auto& worldLV = service_->geant4WorldLogical();

      // Wrap the logical volume in a physical volume placement (world)
      world_ = new G4PVPlacement(nullptr, G4ThreeVector(), &worldLV,
                                 worldLV.GetName(), nullptr, false, 0);

      // The GeoModel tree is redundant once the Geant4 geometry is built: the
      // GeoModel->G4 conversion produced independent G4 objects owned by the G4
      // stores, and nothing consults the service afterwards. Release our
      // reference here — during Construct(), with all G4/GeoModel state still
      // alive and no worker threads running — instead of holding it until
      // program teardown. As the only user this reclaims the tree's memory for
      // the rest of the run and keeps the only non-trivial destructor on the
      // GeoModel path off the teardown path (investigated in the context of
      // issue #68); with other users (the GENIE chain) the shared instance
      // lives until the last of them lets go.
      service_.reset();
    });
    return world_;
  }

  [[nodiscard]] std::vector<SHiP::aegir::SensitiveVolume> const&
  sensitiveVolumes() const override {
    return sensitive_vols_;
  }
};

}  // namespace

PHLEX_REGISTER_PROVIDERS(s, config) {
  using namespace phlex;

  auto db_file = config.get<std::string>("db_file");

  // Resolve relative/bare filenames via SHIPGEOMETRY_ROOT.
  // .filename() intentionally strips subdirectory components — the geometry
  // install layout is flat under share/geometry/.
  if (!std::filesystem::exists(db_file)) {
    auto resolved = std::filesystem::path(db_file).filename();
    if (auto const* root = std::getenv("SHIPGEOMETRY_ROOT")) {
      resolved = std::filesystem::path(root) / "share" / "geometry" / resolved;
    }
    if (std::filesystem::exists(resolved)) {
      db_file = resolved.string();
    } else {
      throw std::runtime_error(
          "Cannot locate geometry DB '" + db_file +
          "'; set SHIPGEOMETRY_ROOT or provide an absolute path");
    }
  }

  auto sv = SHiP::aegir::read_sensitive_volumes(config);

  // Publish as the interface type: consumers request
  // std::shared_ptr<SHiP::aegir::IGeometrySource>.
  std::shared_ptr<SHiP::aegir::IGeometrySource> const source =
      std::make_shared<GeoModelGeometrySource>(db_file, std::move(sv));

  SHiP::aegir::provide_constant(s, "create_geometry", source, "geometry",
                                "detector", "job");
}
