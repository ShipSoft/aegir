// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// pythia_philox_engine.hpp — Philox random numbers for Pythia, keyed per event
//
// Pythia's own generator is one sequential stream, so the event a Pythia
// instance produces depends on how many events it produced before. Phlex
// does not hand a serial source its events in event-number order, so with
// more than one thread the same seed could give different events (issue
// #133). PhiloxRndmEngine replaces Pythia's generator with a PhiloxRng that
// the source re-keys before every event to (seed, stream, event number):
// each event is then a function of the seed and its event number alone, as
// for the particle gun.
//
// Usage: attach with pythia.setRndmEnginePtr() before pythia.init(), and
// reset() with an initialisation key before init() too, since Pythia draws
// random numbers while initialising. Then reset() before each next().
//
// Kept out of pythia_common.hpp, which the standalone Pythia benchmark
// includes without Random123.

#pragma once

#include <Pythia8/Basics.h>

#include <cstdint>

#include "philox_rng.hpp"

namespace aegir {

// Philox stream keys for the Pythia engines, distinct from the other aegir
// streams (particle gun 0xBEEFCAFE, fixed-target sampling 0xF14ED0A7,
// Geant4 0x47345EED).
inline constexpr std::uint32_t kPythiaEventStream = 0x50595448;  // "PYTH"
inline constexpr std::uint32_t kPythiaInitStream = 0x50594E49;   // "PYNI"

class PhiloxRndmEngine : public Pythia8::RndmEngine {
 public:
  // Start a fresh stream: (seed, stream) is the Philox key and ctr1 selects
  // the sub-stream, normally the event number.
  void reset(std::uint32_t seed, std::uint32_t stream, std::uint32_t ctr1) {
    rng_ = PhiloxRng{seed, stream, ctr1};
  }

  // Uniform in (0, 1): Pythia's own generator never returns 0, and some of
  // its samplings take a logarithm of the draw. Pythia's own generator has
  // 48 bits of resolution, so one 32-bit Philox word per draw would be
  // coarser; uniform53() spends two.
  double flat() override {
    double u = rng_.uniform53();
    while (u == 0.0) {
      u = rng_.uniform53();
    }
    return u;
  }

 private:
  PhiloxRng rng_{0};
};

}  // namespace aegir
