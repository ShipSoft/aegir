// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_node_id.hpp — SimHit::geometry_node_id of detector hits
//
// A detector hit's geometry_node_id is the Geant4 copy number `id_depth`
// levels above the volume the step started in: 0 is that volume, 1 its
// mother, and so on, as G4VTouchable counts depth. `id_depth` comes from the
// volume's `sensitive_volumes` entry. The GeoModel->Geant4 conversion turns a
// volume's GeoIdentifierTag into its copy number, so `id_depth` names the
// level whose tag carries the id (a straw's wall, one above its gas).
// check_id_depths() walks the geometry once, before the run, to find depths
// that cannot work. Kept free of the rest of aegir so that tests can use it.

#pragma once

#include <G4LogicalVolume.hh>
#include <G4VPhysicalVolume.hh>
#include <G4VTouchable.hh>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SHiP::aegir::g4 {

/// geometry_node_id of a hit whose volume carries no id.
inline constexpr std::int32_t kNoGeometryNodeId = -1;

/// Copy number GeoModel2G4 gives a placement without a GeoIdentifierTag
/// (GeoModel 6.32). It is GeoModel's choice, not aegir's, and every use goes
/// through this constant, so a GeoModel release that changes it needs only
/// this line changed.
inline constexpr int kGeoModelUntaggedCopyNumber = 16969;

/// The geometry_node_id for `copy_number`: the copy number itself, or
/// kNoGeometryNodeId for an untagged GeoModel placement.
[[nodiscard]] constexpr std::int32_t node_id_from_copy_number(int copy_number) {
  return copy_number == kGeoModelUntaggedCopyNumber ? kNoGeometryNodeId
                                                    : copy_number;
}

/// The geometry_node_id of a hit at `touchable`, read `id_depth` levels above
/// its volume, or kNoGeometryNodeId if the history is not that deep
/// (check_id_depths() rejects such a configuration before the run).
[[nodiscard]] inline std::int32_t geometry_node_id(
    G4VTouchable const& touchable, int id_depth) {
  if (id_depth < 0 || id_depth > touchable.GetHistoryDepth()) {
    return kNoGeometryNodeId;
  }
  return node_id_from_copy_number(touchable.GetCopyNumber(id_depth));
}

/// What check_id_depths() found for one sensitive logical volume.
struct IdDepthFinding {
  enum class Kind {
    too_deep,  ///< in some placement, id_depth reaches above the world
    shared,    ///< several placements, and all of them get the same id
  };
  Kind kind;
  G4LogicalVolume const* volume;
  int id_depth;
  std::size_t placements;  ///< placements of the volume walked
  std::int32_t id;         ///< for `shared`: the id they all get
};

namespace detail {

// Walks the placements under a world and records, per sensitive volume, the
// ids its placements get at its id_depth.
class IdDepthWalk {
 public:
  struct State {
    std::size_t placements = 0;
    std::optional<std::int32_t> first_id;
    bool varies = false;
    bool too_deep = false;
    // The level carrying the id is a replica or parameterised volume, whose
    // copy number exists only while navigating.
    bool replicated = false;
  };

  explicit IdDepthWalk(
      std::unordered_map<G4LogicalVolume const*, int> const& id_depths)
      : id_depths_{&id_depths} {}

  // Depth first, keeping the path from the world to the current placement.
  // A replica or parameterised volume is one G4VPhysicalVolume for all of
  // its copies and is walked once: its copies hold the same placements, so
  // whatever lies inside it gets the same ids in each copy anyway.
  void walk(G4VPhysicalVolume const* world) {
    enter(world);
    while (!path_.empty()) {
      auto const* lv = path_.back()->GetLogicalVolume();
      auto const next = next_daughter_.back();
      if (next == lv->GetNoDaughters()) {
        path_.pop_back();
        next_daughter_.pop_back();
        continue;
      }
      ++next_daughter_.back();
      auto const* daughter = lv->GetDaughter(next);
      if (leads_to_sensitive(daughter->GetLogicalVolume())) {
        enter(daughter);
      }
    }
  }

  [[nodiscard]] std::unordered_map<G4LogicalVolume const*, State> const&
  states() const {
    return states_;
  }

 private:
  void enter(G4VPhysicalVolume const* pv) {
    path_.push_back(pv);
    next_daughter_.push_back(0);
    auto const* lv = pv->GetLogicalVolume();
    if (auto const it = id_depths_->find(lv); it != id_depths_->end()) {
      record(lv, it->second);
    }
  }

  void record(G4LogicalVolume const* lv, int id_depth) {
    auto& state = states_[lv];
    ++state.placements;
    // path_ runs from the world down to this placement, which is depth 0.
    if (id_depth < 0 || std::cmp_greater_equal(id_depth, path_.size())) {
      state.too_deep = true;
      return;
    }
    auto const* carrier =
        path_[path_.size() - 1 - static_cast<std::size_t>(id_depth)];
    if (carrier->IsReplicated()) {
      state.replicated = true;
      return;
    }
    auto const id = node_id_from_copy_number(carrier->GetCopyNo());
    if (!state.first_id) {
      state.first_id = id;
    } else if (*state.first_id != id) {
      state.varies = true;
    }
  }

  // Whether `root` or anything placed inside it is a sensitive volume, so
  // that the walk skips the subtrees that hold none. Answers are kept per
  // logical volume, each worked out once its daughters' are known.
  bool leads_to_sensitive(G4LogicalVolume const* root) {
    std::vector<std::pair<G4LogicalVolume const*, std::size_t>> pending;
    if (!leads_.contains(root)) {
      pending.emplace_back(root, 0);
    }
    while (!pending.empty()) {
      auto const [lv, next] = pending.back();
      if (next < lv->GetNoDaughters()) {
        ++pending.back().second;
        auto const* daughter = lv->GetDaughter(next)->GetLogicalVolume();
        if (!leads_.contains(daughter)) {
          pending.emplace_back(daughter, 0);
        }
        continue;
      }
      bool found = id_depths_->contains(lv);
      for (std::size_t i = 0; i < lv->GetNoDaughters() && !found; ++i) {
        found = leads_.at(lv->GetDaughter(i)->GetLogicalVolume());
      }
      leads_.emplace(lv, found);
      pending.pop_back();
    }
    return leads_.at(root);
  }

  std::unordered_map<G4LogicalVolume const*, int> const* id_depths_;
  std::vector<G4VPhysicalVolume const*> path_;
  // Per entry of path_, the index of the next daughter to walk.
  std::vector<std::size_t> next_daughter_;
  std::unordered_map<G4LogicalVolume const*, State> states_;
  std::unordered_map<G4LogicalVolume const*, bool> leads_;
};

}  // namespace detail

/// Walks every placement under `world` of the logical volumes in
/// `id_depths`, each with its id_depth, and reports a volume whose id_depth
/// reaches above the world in some placement (`too_deep`), or whose
/// placements, more than one, all get the same id (`shared`): its hits could
/// not be told apart. A volume whose id level is a replica or parameterised
/// volume is not checked for `shared`, since those copy numbers exist only
/// while navigating. Findings are ordered by volume name.
[[nodiscard]] inline std::vector<IdDepthFinding> check_id_depths(
    G4VPhysicalVolume const* world,
    std::unordered_map<G4LogicalVolume const*, int> const& id_depths) {
  detail::IdDepthWalk walk{id_depths};
  walk.walk(world);

  std::vector<IdDepthFinding> findings;
  for (auto const& [lv, state] : walk.states()) {
    auto const id_depth = id_depths.at(lv);
    if (state.too_deep) {
      findings.push_back({.kind = IdDepthFinding::Kind::too_deep,
                          .volume = lv,
                          .id_depth = id_depth,
                          .placements = state.placements,
                          .id = kNoGeometryNodeId});
    } else if (state.placements > 1 && !state.varies && !state.replicated &&
               state.first_id) {
      findings.push_back({.kind = IdDepthFinding::Kind::shared,
                          .volume = lv,
                          .id_depth = id_depth,
                          .placements = state.placements,
                          .id = *state.first_id});
    }
  }
  std::ranges::sort(findings, {}, [](IdDepthFinding const& f) {
    return std::string{f.volume->GetName()};
  });
  return findings;
}

}  // namespace SHiP::aegir::g4
