// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// beam_file_source.cpp — Phlex provider plugin reading beam-line particle
// files
//
// Reads the flat particle files the beam-line simulation (BDSIM) provides for
// the P42/BDF line: the beam at the entrance of the BDF shielding and the
// beam-induced background behind the hadron stopper. Each file holds one
// TTree with one entry per particle crossing a plane transverse to the beam:
//   x, y        position in the plane [m]
//   xp, yp, zp  direction cosines
//   p           momentum [GeV/c]
//   t           time [ns]
//   pdgID       PDG code (int)
//   weight      statistical weight
// The entries carry no event structure, and the data model has no
// per-particle weight, so each entry becomes one event: a single status-1
// particle, with the entry's weight in the event header. The plane's z
// position in the aegir frame comes from the configuration, as the file does
// not record it. See docs/beam_files.md.

#include <TDatabasePDG.h>
#include <TFile.h>
#include <TKey.h>
#include <TNamed.h>
#include <TParameter.h>
#include <TParticlePDG.h>
#include <TTree.h>
#include <spdlog/spdlog.h>

#include <SHiP/EventHeader.hpp>
#include <SHiP/MCParticle.hpp>
#include <SHiP/QuantityView.hpp>
#include <SHiP/Units.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "hepmc_status.hpp"
#include "mc_particle_source.hpp"
#include "units/config_units.hpp"

namespace {

namespace su = SHiP::units;

constexpr char const* kName = "beam_file_source";

struct Settings {
  std::string file;
  std::string tree;  // empty: the file's only TTree
  SHiP::Length z;
  SHiP::Length x_offset;
  SHiP::Length y_offset;
  long long first_entry = 0;
  // Where set, each particle is moved along its direction, in a straight
  // line, from the file's plane to this z: for a plane upstream of material
  // the geometry lacks (the vacuum beam pipe, say), so the particles do not
  // cross air that is vacuum in reality.
  std::optional<SHiP::Length> project_to_z;
  bool keep_time = false;
  double weight_scale = 1.0;
};

// One file entry, copied out of the branch buffers under the lock.
struct Entry {
  double x, y, xp, yp, zp, p, t, weight;
  int pdg;
};

class BeamFileSource {
 public:
  explicit BeamFileSource(Settings settings) : s_{std::move(settings)} {
    file_.reset(TFile::Open(s_.file.c_str(), "READ"));
    if (!file_ || file_->IsZombie()) {
      throw std::runtime_error(std::string{kName} + ": cannot open '" +
                               s_.file + "'");
    }
    if (s_.tree.empty()) {
      s_.tree = only_tree_name();
    }
    tree_ = file_->Get<TTree>(s_.tree.c_str());
    if (tree_ == nullptr) {
      throw std::runtime_error(std::string{kName} + ": no TTree '" + s_.tree +
                               "' in '" + s_.file + "'");
    }

    tree_->SetBranchStatus("*", false);
    bind_branch("x", &buf_.x);
    bind_branch("y", &buf_.y);
    bind_branch("xp", &buf_.xp);
    bind_branch("yp", &buf_.yp);
    bind_branch("zp", &buf_.zp);
    bind_branch("p", &buf_.p);
    bind_branch("t", &buf_.t);
    bind_branch("pdgID", &buf_.pdg);
    bind_branch("weight", &buf_.weight);

    log_metadata();
  }

  std::vector<SHiP::MCParticle> generate(phlex::data_cell_index const& id) {
    Entry e{};
    double mass = 0.0;
    {
      // The mc_particles and event_header providers are separate phlex nodes
      // that may run concurrently; the TTree, its branch buffers and
      // TDatabasePDG are not thread-safe, so io_mutex_ serialises them.
      std::scoped_lock const lock{io_mutex_};
      e = load(entry_for(id));
      mass = mass_of(e.pdg);
    }

    auto x = e.x * su::m + s_.x_offset;
    auto y = e.y * su::m + s_.y_offset;
    auto z = s_.z;
    SHiP::Time time = (s_.keep_time ? e.t : 0.0) * su::ns;
    if (s_.project_to_z) {
      if (e.zp <= 0.0) {
        throw std::runtime_error(
            std::string{kName} + ": cannot project entry " +
            std::to_string(entry_for(id)) +
            " to project_to_z, it does not move downstream (zp = " +
            std::to_string(e.zp) + ")");
      }
      auto const path_per_dz = 1.0 / e.zp;  // path length per unit of z
      auto const dz = *s_.project_to_z - s_.z;
      x += dz * (e.xp * path_per_dz);
      y += dz * (e.yp * path_per_dz);
      z = *s_.project_to_z;
      if (s_.keep_time) {
        auto const beta = e.p / std::hypot(e.p, mass);
        time += (dz * path_per_dz / (beta * su::c)).in(su::ns);
      }
    }

    SHiP::MCParticle mc;
    mc.pdg_code = e.pdg;
    SHiP::view::set_vertex(mc, {x, y, z});
    SHiP::view::set_momentum(
        mc, {e.p * e.xp * su::GeV_per_c, e.p * e.yp * su::GeV_per_c,
             e.p * e.zp * su::GeV_per_c});
    SHiP::view::set_energy(mc, std::hypot(e.p, mass) * su::GeV);
    SHiP::view::set_time(mc, time);
    mc.status = SHiP::aegir::hepmc::final_state;
    mc.mother_id = -1;
    return {mc};
  }

  SHiP::EventHeader generate_header(phlex::data_cell_index const& id) {
    auto const entry = entry_for(id);
    double weight = 0.0;
    {
      std::scoped_lock const lock{io_mutex_};
      weight = load(entry).weight;
    }
    return SHiP::EventHeader{
        .weight = weight * s_.weight_scale,
        .original_event_id = static_cast<std::int64_t>(entry)};
  }

 private:
  // The name of the file's only TTree, for files whose tree name (DRIFT_96,
  // HADRON_STOPPER, ...) follows the sampler it was recorded at.
  std::string only_tree_name() const {
    std::vector<std::string> trees;
    for (auto* obj : *file_->GetListOfKeys()) {
      auto const* key = static_cast<TKey const*>(obj);
      if (std::string{key->GetClassName()} == "TTree" &&
          (trees.empty() || trees.back() != key->GetName())) {
        trees.emplace_back(key->GetName());  // cycles of one tree are adjacent
      }
    }
    if (trees.size() != 1) {
      std::string names;
      for (auto const& t : trees) {
        names += (names.empty() ? "" : ", ") + t;
      }
      throw std::runtime_error(std::string{kName} + ": '" + s_.file +
                               "' holds " + std::to_string(trees.size()) +
                               " TTrees (" + names +
                               "); name the one to read with the `tree` key");
    }
    return trees.front();
  }

  // Report what the file says about itself: the producers record the sample's
  // description and its normalisation (protons on target) in the file.
  void log_metadata() const {
    spdlog::info("{}: reading TTree '{}' from '{}' ({} entries, from entry {})",
                 kName, s_.tree, s_.file, tree_->GetEntries(), s_.first_entry);
    if (s_.project_to_z) {
      spdlog::info(
          "{}: particles are moved in straight lines from z = {} mm "
          "to z = {} mm",
          kName, s_.z.numerical_value_in(su::mm),
          s_.project_to_z->numerical_value_in(su::mm));
    }
    if (auto const* d = file_->Get<TNamed>("description")) {
      spdlog::info("{}: description: {}", kName, d->GetTitle());
    }
    if (auto const* p = file_->Get<TParameter<double>>("originalProtons")) {
      spdlog::info("{}: the file stands for {:.6g} protons on target", kName,
                   p->GetVal());
    }
  }

  template <typename T>
  void bind_branch(char const* name, T* address) {
    if (tree_->GetBranch(name) == nullptr) {
      throw std::runtime_error(std::string{kName} + ": branch '" + name +
                               "' missing from TTree '" + s_.tree + "' in '" +
                               s_.file + "'");
    }
    tree_->SetBranchStatus(name, true);
    // Negative means the branch could not be bound to this address (a type
    // mismatch, typically); unchecked, the buffer would silently keep stale
    // values.
    if (tree_->SetBranchAddress(name, address) < 0) {
      throw std::runtime_error(std::string{kName} + ": branch '" + name +
                               "' in '" + s_.file +
                               "' does not have the expected type");
    }
  }

  long long entry_for(phlex::data_cell_index const& id) const {
    return s_.first_entry + static_cast<long long>(id.number());
  }

  // Read an entry, skipping the read when the other provider already loaded
  // it. Caller must hold io_mutex_.
  Entry load(long long entry) {
    if (entry >= tree_->GetEntries()) {
      throw std::runtime_error(
          std::string{kName} +
          ": input exhausted — the workflow requested entry " +
          std::to_string(entry) + " but '" + s_.file + "' holds only " +
          std::to_string(tree_->GetEntries()) +
          " entries. Reduce the driver's event count or first_entry.");
    }
    if (entry != loaded_entry_) {
      if (tree_->GetEntry(entry) <= 0) {
        throw std::runtime_error(std::string{kName} + ": cannot read entry " +
                                 std::to_string(entry) + " of '" + s_.file +
                                 "'");
      }
      loaded_entry_ = entry;
    }
    return buf_;
  }

  // Rest mass in GeV, for the particle's energy (Geant4 derives its own from
  // the momentum, so this only fills the record). Caller must hold io_mutex_.
  double mass_of(int pdg) {
    if (auto it = masses_.find(pdg); it != masses_.end()) {
      return it->second;
    }
    double mass = 0.0;
    if (auto const* particle = TDatabasePDG::Instance()->GetParticle(pdg)) {
      mass = particle->Mass();
    } else {
      spdlog::warn(
          "{}: PDG code {} unknown to TDatabasePDG, its energy is "
          "set to |p| (massless)",
          kName, pdg);
    }
    masses_.emplace(pdg, mass);
    return mass;
  }

  Settings s_;
  // Declaration order matters: the TTree is owned by the file, which must
  // outlive every read.
  std::unique_ptr<TFile> file_;
  TTree* tree_ = nullptr;
  Entry buf_{};
  std::unordered_map<int, double> masses_;

  std::mutex io_mutex_;
  long long loaded_entry_ = -1;
};

}  // namespace

PHLEX_REGISTER_PROVIDERS(s, config) {
  using SHiP::aegir::get_quantity;
  Settings settings;
  settings.file = config.get<std::string>("file");
  settings.tree = config.get<std::string>("tree", std::string{});
  auto const z = config.get_if_present<double>("z");
  if (!z) {
    throw std::runtime_error(
        std::string{kName} +
        ": the `z` key (mm) placing the file's plane in the aegir frame is "
        "required");
  }
  settings.z = *z * su::mm;
  settings.x_offset = get_quantity(config, "x_offset", 0.0 * su::mm);
  settings.y_offset = get_quantity(config, "y_offset", 0.0 * su::mm);
  settings.first_entry = config.get<long>("first_entry", 0L);
  if (settings.first_entry < 0) {
    throw std::runtime_error(std::string{kName} + ": first_entry must be >= 0");
  }
  if (auto const pz = config.get_if_present<double>("project_to_z")) {
    settings.project_to_z = *pz * su::mm;
  }
  settings.keep_time = config.get<bool>("keep_time", false);
  settings.weight_scale = config.get<double>("weight_scale", 1.0);

  auto const source = std::make_shared<BeamFileSource>(std::move(settings));
  SHiP::aegir::provide_mc_particles(
      s,
      [source](phlex::data_cell_index const& id) {
        return source->generate(id);
      },
      phlex::concurrency::serial,
      [source](phlex::data_cell_index const& id) {
        return source->generate_header(id);
      });
}
