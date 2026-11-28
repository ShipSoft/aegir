// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_geomodel_provider.cpp — Phlex provider plugin for GeoModel geometry
//
// Loads a GeoModel .db file via SHiP::GeometryService and converts it to
// Geant4 with SHiP::Geant4Geometry.

#include <G4LogicalVolume.hh>
#include <G4PVPlacement.hh>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "SHiP/GeometryService/Geant4Geometry.h"
#include "SHiP/GeometryService/GeometryService.h"
#include "geometry_node_id.hpp"
#include "geometry_source.hpp"
#include "phlex/source.hpp"
#include "provider_helpers.hpp"

namespace {

class GeoModelGeometrySource : public SHiP::aegir::IGeometrySource {
  std::string db_path_;
  std::vector<SHiP::aegir::SensitiveVolume> sensitive_vols_;
  mutable std::once_flag init_flag_;
  mutable std::shared_ptr<SHiP::Geant4Geometry> geometry_;
  mutable G4VPhysicalVolume* world_ = nullptr;

 public:
  GeoModelGeometrySource(std::string path,
                         std::vector<SHiP::aegir::SensitiveVolume> sv)
      : db_path_{std::move(path)}, sensitive_vols_{std::move(sv)} {}

  [[nodiscard]] G4VPhysicalVolume* construct() const override {
    std::call_once(init_flag_, [this] {
      // The registries share one service, and one GeoModel->G4 conversion
      // of it, with every other user of the same file in this process; in
      // the full GENIE chain, aegir-genie's geometry analyzer has already
      // loaded and converted it (aegir-genie issue #11).
      geometry_ = SHiP::Geant4Geometry::of(
          SHiP::GeometryService::sharedFromFile(db_path_));
      // Throws if the conversion fails, so the volume is never null.
      auto& worldLV = geometry_->worldLogical();

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
      geometry_.reset();
    });
    return world_;
  }

  [[nodiscard]] std::vector<SHiP::aegir::SensitiveVolume> const&
  sensitiveVolumes() const override {
    return sensitive_vols_;
  }

  [[nodiscard]] std::optional<int> untaggedCopyNumber() const override {
    return SHiP::aegir::g4::kGeoModelUntaggedCopyNumber;
  }
};

}  // namespace

PHLEX_REGISTER_PROVIDERS(s, config) {
  using namespace phlex;

  // A bare file name is looked up under $SHIPGEOMETRY_ROOT/share/geometry.
  auto const db_file =
      SHiP::resolveGeometryDbPath(config.get<std::string>("db_file")).string();

  auto sv = SHiP::aegir::read_sensitive_volumes(config);

  // Publish as the interface type: consumers request
  // std::shared_ptr<SHiP::aegir::IGeometrySource>.
  std::shared_ptr<SHiP::aegir::IGeometrySource> const source =
      std::make_shared<GeoModelGeometrySource>(db_file, std::move(sv));

  SHiP::aegir::provide_constant(s, "create_geometry", source, "geometry",
                                "detector", "job");
}
