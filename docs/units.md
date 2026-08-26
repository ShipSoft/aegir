<!--
SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration

SPDX-License-Identifier: LGPL-3.0-or-later
-->

# Unit handling

FairShip was plagued by unit-mismatch bugs; aegir prevents them
structurally. The rules:

## Canonical units and types

The framework-wide vocabulary lives in `SHiP/Units.hpp` (shipdatamodel,
`SHiP::SHiPUnits` target), built on [mp-units](https://mpusz.github.io/mp-units/):

| Quantity        | Type                  | Storage unit |
|-----------------|-----------------------|--------------|
| Length/position | `ship::Length`        | mm           |
| Time            | `ship::Time`          | ns           |
| Energy          | `ship::Energy`        | GeV          |
| Momentum        | `ship::Momentum`      | GeV/c        |
| Mass            | `ship::Mass`          | GeV/c²       |
| Angle           | `ship::Angle`         | rad          |
| Magnetic field  | `ship::MagneticField` | T            |

`GeV/c` and `mm/c` are real derived units built from the exact SI
definition of c, so `energy + momentum` is a compile error and
conversions such as Pythia's `tProd()` (mm/c → ns) are derived rather
than hand-typed. Persisted data-model structs stay plain doubles in the
storage units (ROOT cannot stream quantity types); `SHiP/QuantityView.hpp`
provides typed views over them.

## The one rule at boundaries

**A raw double acquires its unit on the line it enters, and only leaves
a quantity via an explicit conversion.**

- Geant4/CLHEP: only through `src/units/clhep_bridge.hpp` — the single
  file allowed to reference CLHEP unit constants. `G4SystemOfUnits.hh`
  is banned repo-wide (it injects global `mm`, `GeV`, `tesla`, ... that
  collide with the mp-units vocabulary).
- Pythia8 (GeV, mm, mm/c), GENIE (SI): values are wrapped at the read
  site (`p.tProd() * su::mm_per_c`, `vtx[0] * su::m`).
- Configuration: workflow files stay plain numbers; C++ reads them with
  `aegir::get_quantity(config, "beam_energy", 400.0 * su::GeV)`, making
  the parameter definition the single source of truth for the unit.
- Filling a persisted struct is a boundary too: write through the
  `ship::view` setters when the concrete `SHiP::` type is known. In code
  templated over the particle type (e.g. `extract_particles`), build the
  quantity on the read line and unwrap with `ship::raw`. A `// GeV`-style
  comment is not a fence. Copies between persisted structs that share the
  storage units (and diagnostics read straight off them, e.g. histogram
  fills) stay raw — no unit crosses a boundary there.

Never `using namespace ship::units` in a translation unit that includes
CLHEP or Geant4 headers — always qualify.

## Enforcement

prek hooks (see `.pre-commit-config.yaml`) fail on: hardcoded c
(299792458, 299.792458 or 2.99792458e8, digit separators allowed), any
repo-wide `G4SystemOfUnits.hh` include, `CLHEP/Units/` includes or
`CLHEP::` symbols outside `src/units/clhep_bridge.hpp`, and
`1e±3`/`1e±9`-style factors next to unit conversion comments. Review rule: no raw double crosses a function
boundary with an implicit unit — either the parameter is a quantity
type, or the conversion happens on the line the value is read.
