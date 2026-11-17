// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_node_id_test.cpp — check_id_depths() on replicas and placements
//
// Builds a small Geant4 world directly, without GeoModel, with both ways a
// volume is repeated:
//   - Layer (copy 7) > Wall, a G4PVReplica of 4 copies > Gas (copy 0)
//   - Row (copy 1) > Cell, placed 3 times as copies 10, 11, 12 > CellGas (0)
//   - Rack, placed twice (copies 1, 2) > Slot, 3 times in each (copies 0, 1,
//     2: the numbering restarts in every rack) > SlotGas (0)
//   - Strip (copy 0) > Pad, 4 times as copies 20, 99, 99, 21 > PadGas (0)
//   - MixGas (0), both in RepCell, a G4PVReplica of 4 copies in RepRow
//     (copy 3), and in PlCell, placed twice as copy 5 in PlRow (copy 4)
// A replica is one physical volume for all of its copies, so the walk meets
// the Gas once. Its copies must still be counted, and they read the same id
// unless the replica itself is the level that carries it. Any id that more
// than one placement gets is reported, not only one shared by all of them.

#include "geometry_node_id.hpp"

#include <G4Box.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4PVReplica.hh>
#include <G4ThreeVector.hh>
#include <G4VPhysicalVolume.hh>
#include <array>
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
  G4LogicalVolume* slot_gas;
  G4LogicalVolume* pad_gas;
  G4LogicalVolume* mix_gas;
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

  auto* rack = box("Rack", 400, 100, 100);
  for (int r = 1; r <= 2; ++r) {
    new G4PVPlacement(nullptr, G4ThreeVector(0, r == 1 ? 700 : -700, 0), rack,
                      "Rack", world_lv, false, r);
  }
  auto* slot = box("Slot", 100, 100, 100);
  for (int k = 0; k < 3; ++k) {
    new G4PVPlacement(nullptr, G4ThreeVector((k - 1) * 200, 0, 0), slot, "Slot",
                      rack, false, k);
  }
  auto* slot_gas = box("SlotGas", 50, 50, 50);
  new G4PVPlacement(nullptr, G4ThreeVector(), slot_gas, "SlotGas", slot, false,
                    0);

  auto* strip = box("Strip", 400, 100, 100);
  new G4PVPlacement(nullptr, G4ThreeVector(0, 0, 500), strip, "Strip", world_lv,
                    false, 0);
  auto* pad = box("Pad", 100, 100, 100);
  std::array const pad_copies{20, 99, 99, 21};
  double x = -300;
  for (int const copy : pad_copies) {
    new G4PVPlacement(nullptr, G4ThreeVector(x, 0, 0), pad, "Pad", strip, false,
                      copy);
    x += 200;
  }
  auto* pad_gas = box("PadGas", 50, 50, 50);
  new G4PVPlacement(nullptr, G4ThreeVector(), pad_gas, "PadGas", pad, false, 0);

  auto* mix_gas = box("MixGas", 50, 50, 50);
  auto* rep_row = box("RepRow", 400, 100, 100);
  new G4PVPlacement(nullptr, G4ThreeVector(0, 300, -500), rep_row, "RepRow",
                    world_lv, false, 3);
  auto* rep_cell = box("RepCell", 100, 100, 100);
  new G4PVReplica("RepCell", rep_cell, rep_row, kXAxis, 4, 200);
  new G4PVPlacement(nullptr, G4ThreeVector(), mix_gas, "MixGas", rep_cell,
                    false, 0);
  auto* pl_row = box("PlRow", 400, 100, 100);
  new G4PVPlacement(nullptr, G4ThreeVector(0, -300, -500), pl_row, "PlRow",
                    world_lv, false, 4);
  auto* pl_cell = box("PlCell", 100, 100, 100);
  for (int x : {-200, 200}) {
    new G4PVPlacement(nullptr, G4ThreeVector(x, 0, 0), pl_cell, "PlCell",
                      pl_row, false, 5);
  }
  new G4PVPlacement(nullptr, G4ThreeVector(), mix_gas, "MixGas", pl_cell, false,
                    0);

  return {.world = world,
          .gas = gas,
          .cell_gas = cell_gas,
          .slot_gas = slot_gas,
          .pad_gas = pad_gas,
          .mix_gas = mix_gas};
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

// What a single `shared` finding should say: of `placements`, `sharing`
// share an id, and the most shared one is `id`, on `id_placements`.
struct Shared {
  std::size_t placements;
  std::size_t sharing;
  std::optional<std::int32_t> id;
  std::size_t id_placements;
};

// Whether `f` is a single `shared` finding as described by `want`.
bool shared(std::vector<g4::IdDepthFinding> const& f, Shared const& want) {
  return f.size() == 1 && f[0].kind == Kind::shared &&
         f[0].placements == want.placements && f[0].sharing == want.sharing &&
         f[0].id == want.id && f[0].id_placements == want.id_placements;
}

}  // namespace

int main() {
  Checker ok;
  auto const w = build();

  // The replica's copies, walked once, read the same id at any level but
  // their own.
  ok(shared(findings(w, w.gas, 0), {4, 4, 0, 4}),
     "gas at id_depth 0: the 4 wall copies all read the gas's id 0");
  ok(findings(w, w.gas, 1).empty(),
     "gas at id_depth 1: the wall replica carries the id, not compared");
  ok(shared(findings(w, w.gas, 2), {4, 4, 7, 4}),
     "gas at id_depth 2: the 4 wall copies all read the layer's id 7");
  ok(shared(findings(w, w.gas, 3), {4, 4, 0, 4}),
     "gas at id_depth 3: the world, the deepest level there is");
  auto const above = findings(w, w.gas, 4);
  ok(above.size() == 1 && above[0].kind == Kind::too_deep,
     "gas at id_depth 4 reaches above the world");

  // Placements are walked one by one.
  ok(findings(w, w.cell_gas, 1).empty(),
     "cell gas at id_depth 1: cells 10, 11 and 12 are told apart");
  ok(shared(findings(w, w.cell_gas, 0), {3, 3, 0, 3}),
     "cell gas at id_depth 0: all 3 placements get id 0");
  ok(shared(findings(w, w.cell_gas, 0, 0), {3, 3, g4::kNoGeometryNodeId, 3}),
     "the geometry's untagged copy number becomes kNoGeometryNodeId");
  ok(findings(w, w.cell_gas, 1, 11).empty(),
     "one untagged cell next to tagged ones shares no id");

  // Ids that vary but still collide.
  ok(shared(findings(w, w.slot_gas, 1), {6, 6, 0, 2}),
     "slot gas at id_depth 1: slots 0-2 restart in each rack, so all 6 share");
  ok(shared(findings(w, w.slot_gas, 2), {6, 6, 1, 3}),
     "slot gas at id_depth 2: 3 slots per rack id, lowest id named on a tie");
  ok(shared(findings(w, w.pad_gas, 1, 99), {4, 2, g4::kNoGeometryNodeId, 2}),
     "pad gas: 2 untagged pads among tagged ones share -1");
  ok(shared(findings(w, w.pad_gas, 1), {4, 2, 99, 2}),
     "pad gas without an untagged value: copy 99 is given twice");
  ok(shared(findings(w, w.mix_gas, 1), {6, 2, 5, 2}),
     "mix gas: the replica path is skipped, the 2 placements of copy 5 are "
     "still compared");

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
