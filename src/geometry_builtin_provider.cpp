// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_builtin_provider.cpp — Phlex provider plugin for the builtin test
// geometry
//
// Provides the tungsten target + scoring planes geometry as a Job-layer
// product.

#include <G4Box.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "geometry_source.hpp"
#include "phlex/source.hpp"
#include "provider_helpers.hpp"
#include "units/clhep_bridge.hpp"

namespace {

namespace su = ship::units;

// Dimension in Geant4 internal units from a quantity literal.
inline double g4(auto q) { return aegir::clhep::g4(q); }

class BuiltinGeometrySource : public SHiP::IGeometrySource {
  std::vector<std::string> sensitive_vols_{"Scoring"};
  mutable std::once_flag init_flag_;
  mutable G4VPhysicalVolume* world_ = nullptr;

 public:
  [[nodiscard]] G4VPhysicalVolume* construct() const override {
    std::call_once(init_flag_, [this]() {
      auto* nist = G4NistManager::Instance();

      auto* worldMat = nist->FindOrBuildMaterial("G4_AIR");
      auto* worldBox =
          new G4Box("World", g4(5 * su::m), g4(5 * su::m), g4(20 * su::m));
      auto* worldLV = new G4LogicalVolume(worldBox, worldMat, "World");
      world_ = new G4PVPlacement(nullptr, G4ThreeVector(), worldLV, "World",
                                 nullptr, false, 0);

      auto* targetMat = nist->FindOrBuildMaterial("G4_W");
      auto* targetBox = new G4Box("Target", g4(50 * su::mm), g4(50 * su::mm),
                                  g4(500 * su::mm));
      auto* targetLV = new G4LogicalVolume(targetBox, targetMat, "Target");
      new G4PVPlacement(nullptr, G4ThreeVector(0, 0, 0), targetLV, "Target",
                        worldLV, false, 0);

      auto* siMat = nist->FindOrBuildMaterial("G4_Si");
      auto* planeBox = new G4Box("ScoringPlane", g4(2 * su::m), g4(2 * su::m),
                                 g4(0.3 * su::mm));
      auto* planeLV = new G4LogicalVolume(planeBox, siMat, "ScoringPlane");

      std::array<double, 5> z_positions{g4(2 * su::m), g4(4 * su::m),
                                        g4(6 * su::m), g4(8 * su::m),
                                        g4(10 * su::m)};
      for (int i = 0; i < static_cast<int>(z_positions.size()); ++i) {
        new G4PVPlacement(nullptr, G4ThreeVector(0, 0, z_positions[i]), planeLV,
                          "ScoringPlane", worldLV, false, i);
      }
    });
    return world_;
  }

  [[nodiscard]] std::vector<std::string> const& sensitiveVolumes()
      const override {
    return sensitive_vols_;
  }
};

}  // namespace

PHLEX_REGISTER_PROVIDERS(s) {
  using namespace phlex;

  // Shared instance: geometry is constant across all events.
  // The provider returns a shared_ptr copy for each data cell.
  // Publish as the interface type: consumers request
  // std::shared_ptr<SHiP::IGeometrySource>.
  std::shared_ptr<SHiP::IGeometrySource> source =
      std::make_shared<BuiltinGeometrySource>();

  aegir::provide_constant(s, "create_geometry", source, "geometry", "detector",
                          "job");
}
