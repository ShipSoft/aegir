// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// geant4_sim_core.hpp — Shared G4 user action classes and thread-local storage

#pragma once

#include <G4LogicalVolume.hh>
#include <G4Step.hh>
#include <G4StepPoint.hh>
#include <G4Track.hh>
#include <G4UserSteppingAction.hh>
#include <G4UserTrackingAction.hh>
#include <G4VProcess.hh>
#include <G4VSensitiveDetector.hh>
#include <SHiP/QuantityView.hpp>
#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

#include "units/clhep_bridge.hpp"

namespace SHiP::aegir::g4 {

// Thread-local storage for current event data (per G4 worker thread)
inline thread_local std::vector<SimHit> tl_hits;
inline thread_local std::vector<SimParticle> tl_particles;
inline thread_local std::unordered_map<int, std::size_t> tl_track_map;

using DetectorIdMap = std::unordered_map<G4LogicalVolume*, int>;

// Track, particle and pre-step-point kinematics of a hit, under `detector_id`.
inline SimHit make_base_hit(G4Step const* step, int detector_id) {
  auto const* pre = step->GetPreStepPoint();
  auto const pos = pre->GetPosition();
  auto const mom = pre->GetMomentum();

  SimHit hit;
  hit.detector_id = detector_id;
  hit.track_id = step->GetTrack()->GetTrackID();
  hit.pdg_code = step->GetTrack()->GetDefinition()->GetPDGEncoding();
  SHiP::view::set_position(hit, SHiP::aegir::clhep::position(pos));
  SHiP::view::set_momentum(hit, SHiP::aegir::clhep::momentum(mom));
  SHiP::view::set_time(hit, SHiP::aegir::clhep::time(pre->GetGlobalTime()));
  return hit;
}

// As above, with the detector id looked up from the step's logical volume
// (-1 if the volume is not in `detector_ids`).
inline SimHit make_base_hit(G4Step const* step,
                            DetectorIdMap const& detector_ids) {
  auto* lv =
      step->GetPreStepPoint()->GetTouchable()->GetVolume()->GetLogicalVolume();
  auto const it = detector_ids.find(lv);
  return make_base_hit(step, it != detector_ids.end() ? it->second : -1);
}

class ScoringSD : public G4VSensitiveDetector {
 public:
  ScoringSD(G4String const& name, DetectorIdMap detector_ids)
      : G4VSensitiveDetector(name), detector_ids_{std::move(detector_ids)} {}

  G4bool ProcessHits(G4Step* step, G4TouchableHistory*) override {
    double const edep = step->GetTotalEnergyDeposit();
    if (edep <= 0) {
      return false;
    }

    auto hit = make_base_hit(step, detector_ids_);
    SHiP::view::set_energy_deposit(hit, SHiP::aegir::clhep::energy(edep));
    SHiP::view::set_path_length(
        hit, SHiP::aegir::clhep::length(step->GetStepLength()));
    tl_hits.push_back(hit);
    return true;
  }

 private:
  DetectorIdMap detector_ids_;
};

// Records a SimHit when a track first enters the volume, regardless of
// energy deposit. Optionally filters on kinetic energy threshold.
// Matches FairShip's exitHadronAbsorber scoring behaviour.
class CrossingSD : public G4VSensitiveDetector {
 public:
  CrossingSD(G4String const& name, DetectorIdMap detector_ids,
             SHiP::Energy ke_threshold = SHiP::Energy::zero())
      : G4VSensitiveDetector(name),
        detector_ids_{std::move(detector_ids)},
        ke_threshold_{SHiP::aegir::clhep::g4(ke_threshold)} {}

  G4bool ProcessHits(G4Step* step, G4TouchableHistory*) override {
    if (!step->IsFirstStepInVolume()) {
      return false;
    }

    auto const* track = step->GetTrack();
    if (track->GetKineticEnergy() < ke_threshold_) {
      return false;
    }

    auto hit = make_base_hit(step, detector_ids_);
    hit.energy_deposit = 0;
    SHiP::view::set_path_length(
        hit, SHiP::aegir::clhep::length(track->GetTrackLength()));
    tl_hits.push_back(hit);
    return true;
  }

 private:
  DetectorIdMap detector_ids_;
  double ke_threshold_;  // Geant4 internal units
};

// Kills tracks below kinetic energy threshold.
// Matches FairShip's PreTrack() stopping behaviour.
class EnergyCutAction : public G4UserSteppingAction {
 public:
  explicit EnergyCutAction(SHiP::Energy ke_threshold)
      : ke_threshold_{SHiP::aegir::clhep::g4(ke_threshold)} {}

  void UserSteppingAction(const G4Step* step) override {
    auto* track = step->GetTrack();
    if (track->GetKineticEnergy() < ke_threshold_) {
      track->SetTrackStatus(fStopAndKill);
    }
  }

 private:
  double ke_threshold_;  // Geant4 internal units
};

class TrackingAction : public G4UserTrackingAction {
 public:
  explicit TrackingAction(SHiP::Energy particle_ke_cut = SHiP::Energy::zero())
      : particle_ke_cut_{SHiP::aegir::clhep::g4(particle_ke_cut)} {}

  void PreUserTrackingAction(const G4Track* track) override {
    if (particle_ke_cut_ > 0 && track->GetParentID() != 0 &&
        track->GetKineticEnergy() < particle_ke_cut_) {
      return;
    }

    SimParticle p;
    p.track_id = track->GetTrackID();
    p.parent_id = track->GetParentID();
    p.pdg_code = track->GetDefinition()->GetPDGEncoding();

    SHiP::view::set_vertex(p,
                           SHiP::aegir::clhep::position(track->GetPosition()));
    SHiP::view::set_momentum(
        p, SHiP::aegir::clhep::momentum(track->GetMomentum()));
    SHiP::view::set_energy(
        p, SHiP::aegir::clhep::energy(track->GetKineticEnergy()));
    SHiP::view::set_time(p, SHiP::aegir::clhep::time(track->GetGlobalTime()));

    const auto* creator = track->GetCreatorProcess();
    p.creator_process = creator ? creator->GetProcessSubType() : 0;

    tl_track_map[p.track_id] = tl_particles.size();
    tl_particles.push_back(p);
  }

  void PostUserTrackingAction(const G4Track* track) override {
    auto const it = tl_track_map.find(track->GetTrackID());
    if (it != tl_track_map.end()) {
      SHiP::view::set_endpoint(
          tl_particles[it->second],
          SHiP::aegir::clhep::position(track->GetPosition()));
    }
  }

 private:
  double particle_ke_cut_;  // Geant4 internal units
};

// Keep only the particles that left a hit and every ancestor of those, as
// FairShip's stack does (SetMinPoints(1), mothers stored). Order is kept.
// The parent ids of what is kept resolve only if every ancestor was
// recorded, i.e. without a particle_ke_cut.
inline void prune_to_hit_ancestry(std::vector<SimParticle>& particles,
                                  std::vector<SimHit> const& hits) {
  std::unordered_map<int, std::size_t> index;
  index.reserve(particles.size());
  for (std::size_t i = 0; i < particles.size(); ++i) {
    index.emplace(particles[i].track_id, i);
  }
  std::vector<bool> keep(particles.size(), false);
  for (auto const& hit : hits) {
    // Walk up until the primaries (parent 0) or a chain already marked.
    for (auto it = index.find(hit.track_id);
         it != index.end() && !keep[it->second];
         it = index.find(particles[it->second].parent_id)) {
      keep[it->second] = true;
    }
  }
  std::size_t kept = 0;
  for (std::size_t i = 0; i < particles.size(); ++i) {
    if (keep[i]) {
      if (kept != i) {
        particles[kept] = std::move(particles[i]);
      }
      ++kept;
    }
  }
  particles.resize(kept);
}

}  // namespace SHiP::aegir::g4
