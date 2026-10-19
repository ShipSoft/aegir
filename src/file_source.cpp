// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// file_source.cpp — Phlex source plugin
//
// Reads pre-generated events from a ROOT RNTuple, one entry per event, and
// publishes them as the "mc_particles" product. Each entry holds a
// std::vector, so the number of particles per event is arbitrary. This fills
// the role of FairShip's MuonBackGenerator / TTreeGenerator and enables a
// file-driven particle gun.
//
// The RNTuple layout matches what sim_output_module writes: a tuple (default
// name "events") with a std::vector<SHiP::MCParticle> field "mc_particles"
// and, in full-simulation output, a std::vector<SHiP::SimParticle> field
// "sim_particles" and a std::vector<SHiP::MCParticle> field "plane_particles".
// The `product` key names the field to read: "sim_particles" is simulation
// *output*, so it is projected onto MCParticles; any other field is read as
// MCParticles as it is. "plane_particles" restarts a simulation from the
// particles a scoring plane recorded in an earlier stage (only their status-1
// entries are tracked; the rest is their ancestry).
//
// The event header is passed through, and its original_event_id, if unset,
// is filled from the input's event_number, so that each replayed event can
// be traced back to the event it came from even when the earlier stage
// dropped empty events.

#include <TFile.h>
#include <spdlog/spdlog.h>

#include <ROOT/RNTuple.hxx>
#include <ROOT/RNTupleDescriptor.hxx>
#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleView.hxx>
#include <SHiP/EventHeader.hpp>
#include <SHiP/MCParticle.hpp>
#include <SHiP/SimParticle.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mc_particle_source.hpp"
#include "sim_particle_convert.hpp"

namespace {

// Project a simulation SimParticle onto an MCParticle suitable as sim input.
SHiP::MCParticle to_mc_particle(SHiP::SimParticle const& sp) {
  auto mc = SHiP::aegir::to_mc_particle(sp);
  // SimParticle::parent_id is the parent's track id (0 for primaries); map the
  // primary case to MCParticle's -1 convention.
  mc.mother_id = sp.parent_id == 0 ? -1 : sp.parent_id;
  return mc;
}

class FileSource : public phlex::source {
 public:
  FileSource(std::string stage, std::string const& input_file,
             std::string const& ntuple, std::string const& product, long skip)
      : stage_{std::move(stage)},
        skip_{skip},
        read_sim_{product == "sim_particles"} {
    // skip_ is cast to the unsigned ROOT::NTupleSize_t in generate(); reject a
    // negative skip here rather than let it wrap around to a huge offset.
    if (skip < 0) {
      throw std::runtime_error(
          "file_source: 'skip' must be non-negative, got " +
          std::to_string(skip));
    }

    // Open the file through TFile first so its streamer infos are known to
    // ROOT. data-model's I/O rules need them to read files written by an
    // older data-model (e.g. v0.5.0 output read with 0.6); without them the
    // read aborts in RFieldMeta (root-project/root#23146).
    file_.reset(TFile::Open(input_file.c_str()));
    if (!file_ || file_->IsZombie()) {
      throw std::runtime_error("file_source: cannot open '" + input_file + "'");
    }
    const std::unique_ptr<ROOT::RNTuple> anchor{
        file_->Get<ROOT::RNTuple>(ntuple.c_str())};
    if (!anchor) {
      throw std::runtime_error("file_source: no RNTuple '" + ntuple + "' in '" +
                               input_file + "'");
    }
    reader_ = ROOT::RNTupleReader::Open(*anchor);
    n_entries_ = reader_->GetNEntries();
    auto const& descriptor = reader_->GetDescriptor();
    auto const has_field = [&descriptor](std::string const& name) {
      return descriptor.FindFieldId(name) != ROOT::kInvalidDescriptorId;
    };
    if (!has_field(product)) {
      throw std::runtime_error("file_source: '" + input_file +
                               "' has no field '" + product + "' in '" +
                               ntuple + "'");
    }
    if (read_sim_) {
      sim_view_ = reader_->GetView<std::vector<SHiP::SimParticle>>(product);
    } else {
      mc_view_ = reader_->GetView<std::vector<SHiP::MCParticle>>(product);
    }

    // The event header is optional: files written before it existed have no
    // such field, and GetView would throw. Probe the descriptor first and only
    // read it back when present, so older files still replay (publishing the
    // unweighted default header). Warn in that
    // case, so a run that reports weight 1.0 because the input says so is
    // distinguishable from one where the header was simply not there to read.
    if (has_field("event_header")) {
      header_view_ = reader_->GetView<SHiP::EventHeader>("event_header");
    } else {
      spdlog::warn(
          "file_source: '{}' has no 'event_header' field — publishing the "
          "unweighted default (weight 1.0)",
          input_file);
    }
    if (has_field("event_number")) {
      event_number_view_ = reader_->GetView<std::uint64_t>("event_number");
    }
  }

  std::vector<SHiP::MCParticle> generate(phlex::data_cell_index const& id) {
    auto const entry = static_cast<ROOT::NTupleSize_t>(id.number()) +
                       static_cast<ROOT::NTupleSize_t>(skip_);
    if (entry >= n_entries_) {
      spdlog::warn("file_source: entry {} out of range (file has {})", entry,
                   n_entries_);
      return {};
    }

    // The mc_particles and event_header providers become separate phlex nodes
    // that may run concurrently; a single RNTupleReader and its views are not
    // thread-safe, so all reads share io_mutex_. The view returns a reference
    // into its own buffer, so copy out while still holding the lock.
    std::scoped_lock const lock{io_mutex_};
    if (read_sim_) {
      auto const& sim_particles = (*sim_view_)(entry);
      std::vector<SHiP::MCParticle> particles;
      particles.reserve(sim_particles.size());
      for (auto const& sp : sim_particles) {
        particles.push_back(to_mc_particle(sp));
      }
      return particles;
    }
    return (*mc_view_)(entry);
  }

  SHiP::EventHeader generate_header(phlex::data_cell_index const& id) {
    auto const entry = static_cast<ROOT::NTupleSize_t>(id.number()) +
                       static_cast<ROOT::NTupleSize_t>(skip_);
    // Out-of-range mirrors generate()'s empty-particle fallback: publish the
    // unweighted default rather than reading past the end.
    if (entry >= n_entries_) {
      return SHiP::EventHeader{};
    }
    std::scoped_lock const lock{io_mutex_};
    auto header = header_view_ ? (*header_view_)(entry) : SHiP::EventHeader{};
    // Keep an id set by an earlier stage, so it always names the first.
    if (header.original_event_id < 0 && event_number_view_) {
      header.original_event_id =
          static_cast<std::int64_t>((*event_number_view_)(entry));
    }
    return header;
  }

  phlex::provider_bundles create_providers(
      phlex::product_selector const& selector) override {
    // Read events serially (RNTupleReader/view is not thread-safe); io_mutex_
    // additionally guards the reader against the concurrent header provider.
    SHiP::aegir::event_header_generator header_gen{};
    if (header_view_ || event_number_view_) {
      header_gen = [this](phlex::data_cell_index const& id) {
        return generate_header(id);
      };
    }
    return SHiP::aegir::mc_particle_provider_bundles(
        selector, stage_,
        [this](phlex::data_cell_index const& id) { return generate(id); },
        phlex::concurrency::serial, std::move(header_gen));
  }

 private:
  std::string stage_;
  long skip_;
  bool read_sim_;
  ROOT::NTupleSize_t n_entries_{0};
  // Declared before reader_, so it outlives the reader.
  std::unique_ptr<TFile> file_;
  std::unique_ptr<ROOT::RNTupleReader> reader_;
  std::optional<ROOT::RNTupleView<std::vector<SHiP::MCParticle>>> mc_view_;
  std::optional<ROOT::RNTupleView<std::vector<SHiP::SimParticle>>> sim_view_;
  // Set only when the input carries an "event_header" field; absent for files
  // written before the header existed.
  std::optional<ROOT::RNTupleView<SHiP::EventHeader>> header_view_;
  // The writing job's event number, for EventHeader::original_event_id.
  std::optional<ROOT::RNTupleView<std::uint64_t>> event_number_view_;
  // Serialises all reader/view access across the concurrent mc_particles and
  // event_header provider nodes.
  std::mutex io_mutex_;
};

}  // namespace

PHLEX_REGISTER_SOURCE(s, config) {
  using namespace phlex;

  auto const input_file = config.get<std::string>("input_file");
  auto const ntuple = config.get<std::string>("ntuple", std::string{"events"});
  auto const product =
      config.get<std::string>("product", std::string{"mc_particles"});
  auto const skip =
      config.get<long>("skip", 0L);  // start reading at this entry
  auto stage = SHiP::aegir::source_stage(config, "file_source");

  s.add_source<FileSource>("file_source", std::move(stage), input_file, ntuple,
                           product, skip);
}
