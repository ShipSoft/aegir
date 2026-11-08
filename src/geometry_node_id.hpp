// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geometry_node_id.hpp — SimHit::geometry_node_id of detector hits
//
// A detector hit's geometry_node_id is the Geant4 copy number `id_depth`
// levels above the volume the step started in: 0 is that volume, 1 its
// mother, and so on up to the world, as G4VTouchable counts depth.
// `id_depth` comes from the volume's `sensitive_volumes` entry. The
// GeoModel->Geant4 conversion turns a volume's GeoIdentifierTag into its copy
// number, so `id_depth` names the level whose tag carries the id (a straw's
// wall, one above its gas). check_id_depths() walks the geometry once, before
// the run, to find depths that cannot work. Kept free of the rest of aegir so
// that tests can use it.

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
/// (GeoModel 6.32). It is GeoModel's choice, not aegir's, and GeoModel does
/// not export it, so it is named once here; the GeoModel copy-number test
/// fails if a GeoModel release stops producing it. Only the GeoModel geometry
/// source reports it (IGeometrySource::untaggedCopyNumber()): in a GDML or
/// builtin geometry 16969 is an ordinary copy number.
inline constexpr int kGeoModelUntaggedCopyNumber = 16969;

/// The geometry_node_id for `copy_number`: the copy number itself, or
/// kNoGeometryNodeId when it is `untagged`, the copy number the geometry gives
/// a volume that carries no id (unset when it has none).
[[nodiscard]] constexpr std::int32_t node_id_from_copy_number(
    int copy_number, std::optional<int> untagged) {
  return untagged && copy_number == *untagged ? kNoGeometryNodeId : copy_number;
}

/// The geometry_node_id of a hit at `touchable`, read `id_depth` levels above
/// its volume (GetCopyNumber() accepts 0 to GetHistoryDepth(), the world), or
/// kNoGeometryNodeId if the history is not that deep (check_id_depths()
/// rejects such a configuration before the run).
[[nodiscard]] inline std::int32_t geometry_node_id(
    G4VTouchable const& touchable, int id_depth, std::optional<int> untagged) {
  if (id_depth < 0 || id_depth > touchable.GetHistoryDepth()) {
    return kNoGeometryNodeId;
  }
  return node_id_from_copy_number(touchable.GetCopyNumber(id_depth), untagged);
}

/// What a sensitive volume's hits carry: the detector they belong to, and the
/// touchable depth whose copy number is their geometry_node_id.
struct SensitiveIds {
  std::int32_t detector_id;
  int id_depth;
};

/// The sensitive volumes of a geometry, and how their copy numbers become
/// geometry_node_ids. Built once, before the run, and shared by the
/// construction-time check and every thread's sensitive detector.
struct SensitiveVolumeIds {
  std::unordered_map<G4LogicalVolume*, SensitiveIds> volumes;
  /// The geometry's copy number for a volume without an id, if it has one.
  std::optional<int> untagged_copy_number;
};

/// What check_id_depths() found for one sensitive logical volume.
struct IdDepthFinding {
  enum class Kind {
    too_deep,  ///< in some placement, id_depth reaches above the world
    shared,    ///< several placements get the same id
  };
  Kind kind;
  G4LogicalVolume const* volume;
  int id_depth;
  std::size_t placements;  ///< placements of the volume, replica copies too
  /// For `shared`: the id they get, unless it is a replica number, which is
  /// known only while navigating.
  std::optional<std::int32_t> id;
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
    // copy numbers exist only while navigating, so ids cannot be compared
    // across the walked paths.
    bool replicated = false;
    // Copies of a replica or parameterised volume, on the path but not the
    // level carrying the id, read the same id: that is sharing on its own.
    bool shared_by_copies = false;
    std::optional<std::int32_t> shared_id;
  };

  explicit IdDepthWalk(SensitiveVolumeIds const& sensitive)
      : sensitive_{&sensitive} {}

  // Depth first, keeping the path from the world to the current placement.
  // A replica or parameterised volume is one G4VPhysicalVolume for all of
  // its copies and is walked once; record() counts its copies.
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

  [[nodiscard]] std::unordered_map<G4LogicalVolume*, State> const& states()
      const {
    return states_;
  }

 private:
  // The copies a physical volume stands for: 1 for a placement, the number
  // of copies for a replica or parameterised volume.
  static std::size_t copies_of(G4VPhysicalVolume const* pv) {
    return pv->IsReplicated()
               ? static_cast<std::size_t>(std::max(pv->GetMultiplicity(), 1))
               : 1;
  }

  void enter(G4VPhysicalVolume const* pv) {
    path_.push_back(pv);
    next_daughter_.push_back(0);
    auto* lv = pv->GetLogicalVolume();
    if (auto const it = sensitive_->volumes.find(lv);
        it != sensitive_->volumes.end()) {
      record(lv, it->second.id_depth);
    }
  }

  void record(G4LogicalVolume* lv, int id_depth) {
    auto& state = states_[lv];
    // path_ runs from the world down to this placement, which is depth 0.
    if (id_depth < 0 || std::cmp_greater_equal(id_depth, path_.size())) {
      state.too_deep = true;
      ++state.placements;
      return;
    }
    auto const carrier_index =
        path_.size() - 1 - static_cast<std::size_t>(id_depth);
    auto const* carrier = path_[carrier_index];

    // Placements this path stands for that read the same carrier copy.
    std::size_t sharing = 1;
    for (std::size_t k = 0; k < path_.size(); ++k) {
      if (k != carrier_index) {
        sharing *= copies_of(path_[k]);
      }
    }
    state.placements += sharing * copies_of(carrier);

    std::optional<std::int32_t> id;
    if (carrier->IsReplicated()) {
      state.replicated = true;
    } else {
      id = node_id_from_copy_number(carrier->GetCopyNo(),
                                    sensitive_->untagged_copy_number);
      if (!state.first_id) {
        state.first_id = id;
      } else if (*state.first_id != *id) {
        state.varies = true;
      }
    }
    if (sharing > 1 && !state.shared_by_copies) {
      state.shared_by_copies = true;
      state.shared_id = id;
    }
  }

  // Whether `root` or anything placed inside it is a sensitive volume, so
  // that the walk skips the subtrees that hold none. Answers are kept per
  // logical volume, each worked out once its daughters' are known.
  bool leads_to_sensitive(G4LogicalVolume* root) {
    std::vector<std::pair<G4LogicalVolume*, std::size_t>> pending;
    if (!leads_.contains(root)) {
      pending.emplace_back(root, 0);
    }
    while (!pending.empty()) {
      auto const [lv, next] = pending.back();
      if (next < lv->GetNoDaughters()) {
        ++pending.back().second;
        auto* daughter = lv->GetDaughter(next)->GetLogicalVolume();
        if (!leads_.contains(daughter)) {
          pending.emplace_back(daughter, 0);
        }
        continue;
      }
      bool found = sensitive_->volumes.contains(lv);
      for (std::size_t i = 0; i < lv->GetNoDaughters() && !found; ++i) {
        found = leads_.at(lv->GetDaughter(i)->GetLogicalVolume());
      }
      leads_.emplace(lv, found);
      pending.pop_back();
    }
    return leads_.at(root);
  }

  SensitiveVolumeIds const* sensitive_;
  std::vector<G4VPhysicalVolume const*> path_;
  // Per entry of path_, the index of the next daughter to walk.
  std::vector<std::size_t> next_daughter_;
  std::unordered_map<G4LogicalVolume*, State> states_;
  std::unordered_map<G4LogicalVolume*, bool> leads_;
};

}  // namespace detail

/// Walks every placement under `world` of the volumes in `sensitive` and
/// reports a volume whose id_depth reaches above the world in some placement
/// (`too_deep`), or whose placements share an id (`shared`): all of several
/// placements get the same id, or the copies of a replica or parameterised
/// volume on the way to the id level all read the same id. Their hits could
/// not be told apart. When the id level is itself a replica or parameterised
/// volume, its copy numbers exist only while navigating, so ids are not
/// compared across placements. Findings are ordered by volume name.
[[nodiscard]] inline std::vector<IdDepthFinding> check_id_depths(
    G4VPhysicalVolume const* world, SensitiveVolumeIds const& sensitive) {
  detail::IdDepthWalk walk{sensitive};
  walk.walk(world);

  std::vector<IdDepthFinding> findings;
  for (auto const& [lv, state] : walk.states()) {
    auto const id_depth = sensitive.volumes.at(lv).id_depth;
    IdDepthFinding finding{.kind = IdDepthFinding::Kind::too_deep,
                           .volume = lv,
                           .id_depth = id_depth,
                           .placements = state.placements,
                           .id = std::nullopt};
    if (state.too_deep) {
      findings.push_back(finding);
    } else if (state.shared_by_copies) {
      finding.kind = IdDepthFinding::Kind::shared;
      finding.id = state.shared_id;
      findings.push_back(finding);
    } else if (state.placements > 1 && !state.varies && !state.replicated &&
               state.first_id) {
      finding.kind = IdDepthFinding::Kind::shared;
      finding.id = state.first_id;
      findings.push_back(finding);
    }
  }
  std::ranges::sort(findings, {}, [](IdDepthFinding const& f) {
    return std::string{f.volume->GetName()};
  });
  return findings;
}

}  // namespace SHiP::aegir::g4
