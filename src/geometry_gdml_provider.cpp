// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_gdml_provider.cpp — Phlex provider plugin for GDML geometry
//
// Reads a GDML file and provides the parsed geometry as a Job-layer product.
// With `from_geomodel: true`, the file is taken to be exported from a GeoModel
// geometry, whose untagged volumes keep GeoModel2G4's default copy number:
// their hits then get kNoGeometryNodeId, as when the geometry is read from
// GeoModel directly. Off by default, since in other GDML that copy number is
// an ordinary id.

#include <G4GDMLParser.hh>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "geometry_node_id.hpp"
#include "geometry_source.hpp"
#include "phlex/source.hpp"
#include "provider_helpers.hpp"

namespace {

class GDMLGeometrySource : public SHiP::aegir::IGeometrySource {
  std::string gdml_path_;
  std::vector<SHiP::aegir::SensitiveVolume> sensitive_vols_;
  bool from_geomodel_;
  mutable std::once_flag init_flag_;
  mutable G4VPhysicalVolume* world_ = nullptr;

 public:
  GDMLGeometrySource(std::string path,
                     std::vector<SHiP::aegir::SensitiveVolume> sv,
                     bool from_geomodel)
      : gdml_path_{std::move(path)},
        sensitive_vols_{std::move(sv)},
        from_geomodel_{from_geomodel} {}

  [[nodiscard]] G4VPhysicalVolume* construct() const override {
    std::call_once(init_flag_, [this] {
      G4GDMLParser parser;
      parser.Read(gdml_path_, /*validate=*/false);
      world_ = parser.GetWorldVolume();
      if (!world_) {
        throw std::runtime_error("GDMLGeometrySource: no world volume in " +
                                 gdml_path_);
      }
    });
    return world_;
  }

  [[nodiscard]] std::vector<SHiP::aegir::SensitiveVolume> const&
  sensitiveVolumes() const override {
    return sensitive_vols_;
  }

  [[nodiscard]] std::optional<int> untaggedCopyNumber() const override {
    if (!from_geomodel_) {
      return std::nullopt;
    }
    return SHiP::aegir::g4::kGeoModelUntaggedCopyNumber;
  }
};

}  // namespace

PHLEX_REGISTER_PROVIDERS(s, config) {
  using namespace phlex;

  auto const gdml_file = config.get<std::string>("gdml_file");
  auto sv = SHiP::aegir::read_sensitive_volumes(config);
  auto const from_geomodel = config.get<bool>("from_geomodel", false);

  // Publish as the interface type: consumers request
  // std::shared_ptr<SHiP::aegir::IGeometrySource>.
  std::shared_ptr<SHiP::aegir::IGeometrySource> const source =
      std::make_shared<GDMLGeometrySource>(gdml_file, std::move(sv),
                                           from_geomodel);

  SHiP::aegir::provide_constant(s, "create_geometry", source, "geometry",
                                "detector", "job");
}
