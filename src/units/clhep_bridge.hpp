// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// clhep_bridge.hpp — the one place aegir touches CLHEP unit constants
//
// Geant4 works in CLHEP internal units (mm/ns/MeV); the framework works in
// the canonical ShipSoft quantities (SHiP/Units.hpp: mm/ns/GeV, GeV/c). Raw
// Geant4 doubles must acquire their unit here on ingest and only leave a
// quantity via g4() on egress. Do not include G4SystemOfUnits.hh anywhere in
// aegir: it injects unqualified globals (mm, GeV, tesla, ...) that collide
// with the mp-units vocabulary.

#pragma once

#include <CLHEP/Units/SystemOfUnits.h>

#include <G4ThreeVector.hh>
#include <G4Types.hh>
#include <SHiP/Units.hpp>

namespace aegir::clhep {

namespace su = ship::units;

// --- ingest: Geant4 internal double -> canonical quantity -------------------

[[nodiscard]] inline ship::Length length(G4double v) {
  return (v / CLHEP::mm) * su::mm;
}
[[nodiscard]] inline ship::Time time(G4double v) {
  return (v / CLHEP::ns) * su::ns;
}
[[nodiscard]] inline ship::Energy energy(G4double v) {
  return (v / CLHEP::GeV) * su::GeV;
}
// Geant4 momenta are energy-scaled (p*c, internal MeV).
[[nodiscard]] inline ship::Momentum momentum(G4double v) {
  return (v / CLHEP::GeV) * su::GeV_per_c;
}
[[nodiscard]] inline ship::Vec3<ship::Length> position(G4ThreeVector const& v) {
  return {length(v.x()), length(v.y()), length(v.z())};
}
[[nodiscard]] inline ship::Vec3<ship::Momentum> momentum(
    G4ThreeVector const& v) {
  return {momentum(v.x()), momentum(v.y()), momentum(v.z())};
}

// --- egress: canonical quantity -> Geant4 internal double -------------------

[[nodiscard]] inline G4double g4(ship::Length l) {
  return l.numerical_value_in(su::mm) * CLHEP::mm;
}
[[nodiscard]] inline G4double g4(ship::Time t) {
  return t.numerical_value_in(su::ns) * CLHEP::ns;
}
[[nodiscard]] inline G4double g4(ship::Energy e) {
  return e.numerical_value_in(su::GeV) * CLHEP::GeV;
}
// The one consciously-bridged c convention: a GeV/c quantity becomes the
// numeric GeV value Geant4 expects for momenta.
[[nodiscard]] inline G4double g4(ship::Momentum p) {
  return p.numerical_value_in(su::GeV_per_c) * CLHEP::GeV;
}

}  // namespace aegir::clhep
