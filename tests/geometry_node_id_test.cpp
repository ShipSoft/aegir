// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_node_id_test.cpp — check_id_depths() on replicas and placements
//
// Builds a small Geant4 world directly, without GeoModel, with both ways a
// volume is repeated:
//   - Layer (copy 7) > Wall, a G4PVReplica of 4 copies > Gas (copy 0)
//   - Row (copy 1) > Cell, placed 3 times as copies 10, 11, 12 > CellGas (0)
// A replica is one physical volume for all of its copies, so the walk meets
// the Gas once. Its copies must still be counted, and they read the same id
// unless the replica itself is the level that carries it.

#include "geometry_node_id.hpp"

#include <G4Box.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4PVReplica.hh>
#include <G4ThreeVector.hh>
#include <G4VPhysicalVolume.hh>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <vector>

#include "checker.hpp"

namespace {

namespace g4 = SHiP::aegir::g4;
using SHiP::aegir::test::Checker;
using Kind = g4::IdDepthFinding::Kind;

struct World {
  G4VPhysicalVolume* world;
  G4LogicalVolume* gas;
  G4LogicalVolume* cell_gas;
};

// A vacuum box `name` of half-widths `hx`, `hy`, `hz` (mm).
G4LogicalVolume* box(char const* name, double hx, double hy, double hz) {
  static auto* const vacuum =
      G4NistManager::Instance()->FindOrBuildMaterial("G4_Galactic");
  return new G4LogicalVolume(new G4Box(name, hx, hy, hz), vacuum, name);
}

// The world described at the top of this file. Geant4's stores own it.
World build() {
  auto* world_lv = box("World", 1000, 1000, 1000);
  auto* world = new G4PVPlacement(nullptr, G4ThreeVector(), world_lv, "World",
                                  nullptr, false, 0);

  auto* layer = box("Layer", 400, 100, 100);
  new G4PVPlacement(nullptr, G4ThreeVector(0, 300, 0), layer, "Layer", world_lv,
                    false, 7);
  auto* wall = box("Wall", 100, 100, 100);
  new G4PVReplica("Wall", wall, layer, kXAxis, 4, 200);
  auto* gas = box("Gas", 50, 50, 50);
  new G4PVPlacement(nullptr, G4ThreeVector(), gas, "Gas", wall, false, 0);

  auto* row = box("Row", 400, 100, 100);
  new G4PVPlacement(nullptr, G4ThreeVector(0, -300, 0), row, "Row", world_lv,
                    false, 1);
  auto* cell = box("Cell", 100, 100, 100);
  for (int k = 0; k < 3; ++k) {
    new G4PVPlacement(nullptr, G4ThreeVector((k - 1) * 200, 0, 0), cell, "Cell",
                      row, false, 10 + k);
  }
  auto* cell_gas = box("CellGas", 50, 50, 50);
  new G4PVPlacement(nullptr, G4ThreeVector(), cell_gas, "CellGas", cell, false,
                    0);
  return {.world = world, .gas = gas, .cell_gas = cell_gas};
}

// check_id_depths() with only `lv` sensitive, at `id_depth`, in a geometry
// whose copy number for a volume without an id is `untagged`.
std::vector<g4::IdDepthFinding> findings(
    World const& w, G4LogicalVolume* lv, int id_depth,
    std::optional<int> untagged = std::nullopt) {
  g4::SensitiveVolumeIds const sensitive{
      .volumes = {{lv, {.detector_id = 0, .id_depth = id_depth}}},
      .untagged_copy_number = untagged};
  return g4::check_id_depths(w.world, sensitive);
}

// Whether `f` is a single `shared` finding of `placements` placements that
// get id `id`.
bool shared(std::vector<g4::IdDepthFinding> const& f, std::size_t placements,
            std::optional<std::int32_t> id) {
  return f.size() == 1 && f[0].kind == Kind::shared &&
         f[0].placements == placements && f[0].id == id;
}

}  // namespace

int main() {
  Checker ok;
  auto const w = build();

  // The replica's copies, walked once, read the same id at any level but
  // their own.
  ok(shared(findings(w, w.gas, 0), 4, 0),
     "gas at id_depth 0: the 4 wall copies all read the gas's id 0");
  ok(findings(w, w.gas, 1).empty(),
     "gas at id_depth 1: the wall replica carries the id, not compared");
  ok(shared(findings(w, w.gas, 2), 4, 7),
     "gas at id_depth 2: the 4 wall copies all read the layer's id 7");
  ok(shared(findings(w, w.gas, 3), 4, 0),
     "gas at id_depth 3: the world, the deepest level there is");
  auto const above = findings(w, w.gas, 4);
  ok(above.size() == 1 && above[0].kind == Kind::too_deep,
     "gas at id_depth 4 reaches above the world");

  // Placements are walked one by one.
  ok(findings(w, w.cell_gas, 1).empty(),
     "cell gas at id_depth 1: cells 10, 11 and 12 are told apart");
  ok(shared(findings(w, w.cell_gas, 0), 3, 0),
     "cell gas at id_depth 0: all 3 placements get id 0");
  ok(shared(findings(w, w.cell_gas, 0, 0), 3, g4::kNoGeometryNodeId),
     "the geometry's untagged copy number becomes kNoGeometryNodeId");
  ok(findings(w, w.cell_gas, 1, 11).empty(),
     "one untagged cell next to tagged ones shares no id");

  g4::SensitiveVolumeIds const both{
      .volumes = {{w.gas, {.detector_id = 0, .id_depth = 0}},
                  {w.cell_gas, {.detector_id = 0, .id_depth = 0}}},
      .untagged_copy_number = std::nullopt};
  auto const two = g4::check_id_depths(w.world, both);
  ok(two.size() == 2 && two[0].volume == w.cell_gas && two[1].volume == w.gas,
     "findings come in volume-name order");

  if (ok.failures == 0) {
    std::println("geometry_node_id: all checks passed");
  }
  return ok.failures == 0 ? 0 : 1;
}
