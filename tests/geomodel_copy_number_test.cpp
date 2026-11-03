// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geomodel_copy_number_test.cpp — what a hit's touchable reads after the
// GeoModel->Geant4 conversion
//
// Builds a small GeoModel world with the identifier patterns the SHiP
// geometry uses, converts it with SHiP::GeometryService as
// geometry_geomodel_provider does, once from the tree and once through a
// GeoModelIO .db file, and reads the copy number at every depth of the
// touchable at a point inside each volume, which is what make_base_hit sees.
//
//   leaf      box with GeoIdentifierTag(42) in the world (the timing bars)
//   untagged  box without a tag
//   straw     sub-layer (tag 3) holding only GeoSerialIdentifier(1000) and a
//             GeoSerialTransformer of 5 copies of one shared wall, which
//             holds a gas box with GeoIdentifierTag(0) (the straw tubes)
//   mixed     layer (tag 4) holding a tagged frame (tag 9) next to
//             GeoSerialIdentifier(2000) and a transformer of 3 cells
//   fibre     cladding (tag 77) holding a core (tag 78) (calorimeter fibres)
//
// Every case prints what it reads and checks what the SHiP numbering relies
// on. GeoModel 6.32 gives the k-th copy of a serial transformer the copy
// number base + k, also when the transformer is alone in its parent, so a
// straw's id is its wall's copy number, one level above the gas.

#include <GeoGenericFunctions/Variable.h>
#include <GeoModelDBManager/GMDBManager.h>
#include <GeoModelKernel/GeoBox.h>
#include <GeoModelKernel/GeoDefinitions.h>
#include <GeoModelKernel/GeoElement.h>
#include <GeoModelKernel/GeoIdentifierTag.h>
#include <GeoModelKernel/GeoLogVol.h>
#include <GeoModelKernel/GeoMaterial.h>
#include <GeoModelKernel/GeoNameTag.h>
#include <GeoModelKernel/GeoPhysVol.h>
#include <GeoModelKernel/GeoSerialIdentifier.h>
#include <GeoModelKernel/GeoSerialTransformer.h>
#include <GeoModelKernel/GeoTransform.h>
#include <GeoModelKernel/GeoXF.h>
#include <GeoModelKernel/Units.h>
#include <GeoModelWrite/WriteGeoModel.h>
#include <SHiP/GeometryService/GeometryService.h>
#include <unistd.h>

#include <G4GeometryManager.hh>
#include <G4LogicalVolume.hh>
#include <G4Navigator.hh>
#include <G4PVPlacement.hh>
#include <G4ThreeVector.hh>
#include <G4TouchableHistory.hh>
#include <G4VPhysicalVolume.hh>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace GU = GeoModelKernelUnits;

/// Copy number GeoModel2G4 gives a placement without an identifier tag.
constexpr int kUntaggedCopyNumber = 16969;

constexpr int kStrawBase = 1000;  // GeoSerialIdentifier of the straw layer
constexpr int kStrawCopy = 2;     // the straw the probe point is in
constexpr int kMixedBase = 2000;  // GeoSerialIdentifier of the mixed layer
constexpr int kMixedCopy = 1;     // the cell the probe point is in

// Counts and reports failed checks.
struct Checker {
  int failures = 0;

  void operator()(bool ok, std::string const& what) {
    if (!ok) {
      std::println(stderr, "FAILED: {}", what);
      ++failures;
    }
  }
};

GeoPhysVol* box(std::string const& name, double hx, double hy, double hz,
                GeoMaterial const* material) {
  return new GeoPhysVol(new GeoLogVol(name, new GeoBox(hx, hy, hz), material));
}

// Places `child` in `mother` at `t`, with an identifier tag unless `id` is
// negative.
void place(GeoPhysVol* mother, GeoVPhysVol* child, std::string const& name,
           int id, GeoTrf::Transform3D const& t) {
  mother->add(new GeoNameTag(name));
  if (id >= 0) {
    mother->add(new GeoIdentifierTag(id));
  }
  mother->add(new GeoTransform(t));
  mother->add(child);
}

// `copies` copies of `unit` along x, at x = -200, -100, ... mm, as the
// tracker factory places its straws.
GeoSerialTransformer* row(GeoPhysVol const* unit, unsigned copies) {
  GeoGenfun::Variable i;
  GeoGenfun::GENFUNCTION x = -200.0 * GU::mm + 100.0 * GU::mm * i;
  GeoXF::TRANSFUNCTION xf = GeoXF::Pow(
      GeoTrf::Transform3D(GeoTrf::Translate3D(1.0 * GU::mm, 0.0, 0.0)), x);
  return new GeoSerialTransformer(unit, &xf, copies);
}

PVConstLink buildWorld() {
  auto* nitrogen =
      new GeoElement("Nitrogen", "N", 7, 14.007 * GU::g / GU::mole);
  auto* air = new GeoMaterial("ProbeAir", 1.29e-3 * GU::g / GU::cm3);
  air->add(nitrogen, 1.0);
  air->lock();

  auto* world = box("ProbeWorld", 1000, 1000, 1000, air);
  using GeoTrf::Translate3D;

  place(world, box("Leaf", 50, 50, 50, air), "Leaf", 42,
        Translate3D(0, 0, -800));
  place(world, box("Untagged", 50, 50, 50, air), "Untagged", -1,
        Translate3D(0, 0, -600));

  // One wall subtree shared by every copy, with the gas tagged 0 inside it.
  auto* wall = box("StrawWall", 20, 40, 40, air);
  place(wall, box("StrawGas", 15, 35, 35, air), "StrawGas", 0,
        GeoTrf::Transform3D::Identity());
  auto* sublayer = box("StrawSubLayer", 300, 50, 50, air);
  sublayer->add(new GeoSerialIdentifier(kStrawBase));
  sublayer->add(row(wall, 5));
  place(world, sublayer, "StrawSubLayer", 3, Translate3D(0, 0, -200));

  auto* mixed = box("MixedLayer", 300, 50, 50, air);
  place(mixed, box("MixedFrame", 20, 40, 40, air), "MixedFrame", 9,
        Translate3D(250, 0, 0));
  mixed->add(new GeoSerialIdentifier(kMixedBase));
  mixed->add(row(box("MixedCell", 20, 40, 40, air), 3));
  place(world, mixed, "MixedLayer", 4, Translate3D(0, 0, 200));

  auto* clad = box("FibreClad", 40, 40, 40, air);
  place(clad, box("FibreCore", 30, 30, 30, air), "FibreCore", 78,
        GeoTrf::Transform3D::Identity());
  place(world, clad, "FibreClad", 77, Translate3D(0, 0, 600));

  return world;
}

/// One level of a touchable history, innermost first.
struct Level {
  std::string volume;
  int copy_number;
  bool parameterised;
};

struct Probe {
  std::string_view name;
  G4ThreeVector point;
};

// Each case's probe point, in the world frame (mm). Not constexpr:
// G4ThreeVector is not a literal type.
std::array<Probe, 5> const kProbes{{
    {"leaf", {0, 0, -800}},
    {"untagged", {0, 0, -600}},
    {"straw", {-200.0 + 100.0 * kStrawCopy, 0, -200}},
    {"mixed", {-200.0 + 100.0 * kMixedCopy, 0, 200}},
    {"fibre", {0, 0, 600}},
}};

// Converts `svc`'s tree and reads the touchable at every probe point.
std::vector<std::vector<Level>> read_touchables(SHiP::GeometryService& svc) {
  G4LogicalVolume& world_lv = svc.geant4WorldLogical();
  auto* world =
      new G4PVPlacement(nullptr, G4ThreeVector(), &world_lv,
                        world_lv.GetName() + "_probe", nullptr, false, 0);
  // Parameterised volumes are navigated through their voxels.
  G4GeometryManager::GetInstance()->CloseGeometry(true, false, world);
  G4Navigator navigator;
  navigator.SetWorldVolume(world);

  std::vector<std::vector<Level>> result;
  for (auto const& probe : kProbes) {
    navigator.LocateGlobalPointAndSetup(probe.point, nullptr, false, true);
    std::unique_ptr<G4TouchableHistory> const touchable{
        navigator.CreateTouchableHistory()};
    std::vector<Level> levels;
    for (int depth = 0; depth < touchable->GetHistoryDepth(); ++depth) {
      auto const* pv = touchable->GetVolume(depth);
      levels.push_back({.volume = pv->GetLogicalVolume()->GetName(),
                        .copy_number = touchable->GetCopyNumber(depth),
                        .parameterised = pv->IsParameterised()});
    }
    result.push_back(std::move(levels));
  }

  G4GeometryManager::GetInstance()->OpenGeometry(world);
  return result;
}

void print(std::string_view source,
           std::vector<std::vector<Level>> const& touchables) {
  std::println("--- converted {} ---", source);
  for (std::size_t i = 0; i < kProbes.size(); ++i) {
    std::print("{:9}", kProbes[i].name);
    for (auto const& level : touchables[i]) {
      std::print(" | {} {}{}", level.volume, level.copy_number,
                 level.parameterised ? " (parameterised)" : "");
    }
    std::println("");
  }
}

int copy_number(std::vector<std::vector<Level>> const& touchables,
                std::size_t probe, std::size_t depth) {
  auto const& levels = touchables.at(probe);
  return depth < levels.size() ? levels[depth].copy_number : -1;
}

void check(Checker& ok, std::string_view source,
           std::vector<std::vector<Level>> const& t) {
  auto const at = [&](std::size_t probe, std::size_t depth) {
    return copy_number(t, probe, depth);
  };
  auto const what = [&](std::string_view s) {
    return std::format("{}: {}", source, s);
  };
  ok(at(0, 0) == 42, what("a tagged leaf has its tag as copy number"));
  ok(at(1, 0) == kUntaggedCopyNumber,
     what("an untagged placement has copy number 16969"));
  ok(at(2, 0) == 0, what("the straw gas has the tag 0 of its placement"));
  ok(at(2, 1) == kStrawBase + kStrawCopy,
     what("the straw wall is numbered base + copy, one level above the gas"));
  ok(at(2, 2) == 3, what("the sub-layer keeps its tag above the straws"));
  ok(at(3, 0) == kMixedBase + kMixedCopy,
     what("a cell placed next to a tagged sibling is numbered base + copy"));
  ok(at(4, 0) == 78 && at(4, 1) == 77,
     what("a tagged core inside a tagged cladding keeps both tags"));
}

}  // namespace

int main() {
  Checker ok;

  auto direct = SHiP::GeometryService::fromWorld(buildWorld());
  auto const from_tree = read_touchables(*direct);
  print("from the GeoModel tree", from_tree);
  check(ok, "tree", from_tree);

  auto const db_path = std::filesystem::temp_directory_path() /
                       std::format("aegir_copy_numbers_{}.db", getpid());
  std::filesystem::remove(db_path);
  {
    GMDBManager db{db_path.string()};
    ok(db.checkIsDBOpen(), "the .db file opens for writing");
    GeoModelIO::WriteGeoModel writer{db};
    buildWorld()->exec(&writer);
    writer.saveToDB();
  }
  auto from_file = SHiP::GeometryService::fromFile(db_path);
  auto const from_db = read_touchables(*from_file);
  print("through a .db file", from_db);
  check(ok, "db", from_db);
  std::filesystem::remove(db_path);

  // Whatever the converter does, the .db must not change it.
  for (std::size_t i = 0; i < kProbes.size(); ++i) {
    for (std::size_t depth = 0; depth < 3; ++depth) {
      ok(copy_number(from_tree, i, depth) == copy_number(from_db, i, depth),
         std::format("{} depth {}: same copy number from tree and .db",
                     kProbes[i].name, depth));
    }
  }

  std::println("{} problem(s)", ok.failures);
  return ok.failures == 0 ? 0 : 1;
}
