// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// detector_construction.hpp — Bridges IGeometrySource to
// G4VUserDetectorConstruction
//
// Delegates Construct() to IGeometrySource::construct() and assigns the
// configured SD type to volumes matching IGeometrySource::sensitiveVolumes(),
// whose hits carry that entry's detector id.
// Registers the configured scoring planes as a parallel world
// (ScoringParallelWorld, see scoring_planes.hpp).

#pragma once

#include <spdlog/spdlog.h>

#include <G4FieldManager.hh>
#include <G4LogicalVolumeStore.hh>
#include <G4ProductionCuts.hh>
#include <G4Region.hh>
#include <G4SDManager.hh>
#include <G4String.hh>
#include <G4VUserDetectorConstruction.hh>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SHiP/FieldService/G4MagFieldAdapter.h"
#include "SHiP/FieldService/IFieldSource.h"
#include "geant4_sim_core.hpp"
#include "geometry_node_id.hpp"
#include "geometry_source.hpp"
#include "scoring_planes.hpp"

namespace SHiP::aegir::g4 {

enum class SDMode { scoring, crossing };

class ConfigurableDetectorConstruction : public G4VUserDetectorConstruction {
  IGeometrySource const*
      source_;  // non-owning; Job-layer product outlives the G4 run
  SHiP::IFieldSource const* field_source_;  // non-owning; may have no regions
  SDMode sd_mode_;
  SHiP::Energy ke_threshold_;  // used by CrossingSD
  std::vector<std::pair<std::string, SHiP::Length>>
      regions_;  // pattern -> production cut
  // The scoring planes' parallel world, or null without planes. Owned by
  // G4VUserDetectorConstruction once registered (leaked with it, like the
  // rest of the Geant4 setup).
  ScoringParallelWorld* scoring_world_ = nullptr;

  // The detector geometry's logical volumes: the volume store without the
  // scoring planes' parallel world, which the sensitive-volume and field
  // patterns must not match.
  [[nodiscard]] std::vector<G4LogicalVolume*> detector_volumes() const {
    std::vector<G4LogicalVolume*> volumes;
    for (auto* lv : *G4LogicalVolumeStore::GetInstance()) {
      if (scoring_world_ == nullptr || !scoring_world_->contains(lv)) {
        volumes.push_back(lv);
      }
    }
    return volumes;
  }

  // The `sensitive_volumes` entry of `lv`: the first one whose pattern its
  // name contains, or null.
  [[nodiscard]] SensitiveVolume const* sensitive_entry(
      G4LogicalVolume const* lv) const {
    for (auto const& sv : source_->sensitiveVolumes()) {
      if (G4StrUtil::contains(lv->GetName(), std::string_view{sv.pattern})) {
        return &sv;
      }
    }
    return nullptr;
  }

  // Fails if a sensitive volume's id_depth reaches above the world in some
  // placement, and warns when all placements of a sensitive volume get the
  // same geometry_node_id, so that a wrong id_depth shows before the run
  // rather than in the output.
  void check_sensitive_ids(G4VPhysicalVolume const* world) const {
    std::unordered_map<G4LogicalVolume const*, int> id_depths;
    std::unordered_map<G4LogicalVolume const*, SensitiveVolume const*> entries;
    for (auto* lv : detector_volumes()) {
      if (auto const* sv = sensitive_entry(lv)) {
        id_depths.emplace(lv, sv->id_depth);
        entries.emplace(lv, sv);
      }
    }
    std::string too_deep;
    for (auto const& f : check_id_depths(world, id_depths)) {
      std::string const name = f.volume->GetName();
      auto const& pattern = entries.at(f.volume)->pattern;
      if (f.kind == IdDepthFinding::Kind::too_deep) {
        too_deep += std::format("\n  '{}' (pattern '{}'): id_depth {}", name,
                                pattern, f.id_depth);
        continue;
      }
      spdlog::warn(
          "geant4: all {} placements of sensitive volume '{}' (pattern '{}') "
          "get geometry_node_id {} at id_depth {}, so their hits cannot be "
          "told apart; set id_depth to the level whose copy number "
          "identifies them",
          f.placements, name, pattern, f.id, f.id_depth);
    }
    if (!too_deep.empty()) {
      throw std::runtime_error(
          "sensitive_volumes: id_depth reaches above the world volume for "
          "some placements of" +
          too_deep);
    }
  }

 public:
  ConfigurableDetectorConstruction(
      IGeometrySource const& source, SHiP::IFieldSource const& field_source,
      SDMode sd_mode = SDMode::scoring,
      SHiP::Energy ke_threshold = SHiP::Energy::zero(),
      std::vector<std::pair<std::string, SHiP::Length>> regions = {},
      std::vector<ScoringPlaneConfig> scoring_planes = {})
      : source_{&source},
        field_source_{&field_source},
        sd_mode_{sd_mode},
        ke_threshold_{ke_threshold},
        regions_{std::move(regions)} {
    if (!scoring_planes.empty()) {
      scoring_world_ = new ScoringParallelWorld(std::move(scoring_planes));
      RegisterParallelWorld(scoring_world_);
    }
  }

  /// Whether scoring planes are configured, i.e. the run needs
  /// G4ParallelWorldPhysics(kScoringWorldName).
  [[nodiscard]] bool has_scoring_planes() const {
    return scoring_world_ != nullptr;
  }

  // Called once, on SHiP::geometry_thread() (the run manager is initialised
  // there — the process's single Geant4 geometry-creating thread)
  G4VPhysicalVolume* Construct() override {
    auto* world = source_->construct();

    // Create the configured production-cut regions here, not in
    // ConstructSDandField: G4Region is a split class, so regions must be
    // created on the process's single geometry-creating thread — this one —
    // while ConstructSDandField runs on worker threads (#80). Construct()
    // is also the canonical Geant4 place: the regions exist before any
    // worker builds its per-region physics tables.
    for (auto const& [pattern, cut] : regions_) {
      auto* region = new G4Region(pattern);
      auto* cuts = new G4ProductionCuts();
      cuts->SetProductionCut(SHiP::aegir::clhep::g4(cut));
      region->SetProductionCuts(cuts);
      bool matched = false;
      for (auto* lv : *G4LogicalVolumeStore::GetInstance()) {
        if (G4StrUtil::contains(lv->GetName(), std::string_view{pattern})) {
          region->AddRootLogicalVolume(lv);
          matched = true;
        }
      }
      if (!matched) {
        throw std::runtime_error("Production-cut region '" + pattern +
                                 "' matches no logical volumes");
      }
    }

    // Here rather than in ConstructSDandField, which runs once per worker
    // thread: the geometry is the same for all of them, so check it once.
    check_sensitive_ids(world);

    return world;
  }

  void ConstructSDandField() override {
    // The detector ids were resolved when the geometry provider read its
    // configuration (read_sensitive_volumes), and Construct() checked the
    // id_depths, so nothing here can fail.
    SensitiveVolumeMap sensitive;

    auto const volumes = detector_volumes();
    for (auto* lv : volumes) {
      if (auto const* sv = sensitive_entry(lv)) {
        sensitive.emplace(lv, SensitiveIds{.detector_id = sv->detector_id,
                                           .id_depth = sv->id_depth});
      }
    }

    G4VSensitiveDetector* sd = nullptr;
    if (sd_mode_ == SDMode::crossing) {
      sd = new CrossingSD("CrossingSD", sensitive, ke_threshold_);
    } else {
      sd = new ScoringSD("ScoringSD", sensitive);
    }
    G4SDManager::GetSDMpointer()->AddNewDetector(sd);

    for (auto [lv, _] : sensitive) {
      lv->SetSensitiveDetector(sd);
    }

    // Install per-magnet G4FieldManagers on every logical volume whose name
    // contains the configured pattern. Outside these volumes Geant4 never
    // invokes the field, so drift regions stay cost-free. An unmatched
    // pattern is treated as a hard configuration error (wrong field setup is
    // a silent physics bug otherwise).
    for (auto const& fr : field_source_->regions()) {
      auto* adapter = new SHiP::G4MagFieldAdapter(fr.field);
      auto* fmgr = new G4FieldManager(adapter);
      bool matched = false;
      for (auto* lv : volumes) {
        if (G4StrUtil::contains(lv->GetName(),
                                std::string_view{fr.volume_pattern})) {
          lv->SetFieldManager(fmgr, /*forceToAllDaughters=*/true);
          matched = true;
        }
      }
      if (!matched) {
        throw std::runtime_error("Field region '" + fr.name +
                                 "': volume_pattern '" + fr.volume_pattern +
                                 "' matches no logical volumes");
      }
    }
  }
};

}  // namespace SHiP::aegir::g4
