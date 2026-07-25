# Changelog

All notable changes to this project will be documented in this file.

## [0.3.0] - 2026-07-25

### Features

- *(doxygen)* Add API documentation via Doxygen and GitHub Pages
- Migrate to Phlex 0.3.0 provider/selector API
- Rework event generators as Phlex sources
- *(lint)* Enforce conventional commits with commitizen
- Document opt-in multi-package dev environment
- Add file_source to read events from a ROOT RNTuple
- Add GENIE rootracker reader source
- Add FairShip cbmsim to flux ntuple converter
- Validate --selection in the flux converter
- Support counter sub-streams in PhiloxRng
- *(geant4)* Add optional GDML export of the constructed geometry
- *(geant4)* Default worker concurrency to framework parallelism
- Share the Geant4 geometry and its creator thread with other plugins
- Route all Geant4 unit conversions through a CLHEP bridge
- Type the Pythia8 boundary with canonical quantities
- *(geant4)* Log simulation progress every N events
- Type the GENIE and particle-gun boundaries
- *(lint)* Fence unit conventions with prek hooks
- Add user seed in fixed target generator
- *(geant4)* Seed the RNG per event from the data-cell index
- *(geant4)* Draw a random seed when none is configured
- *(workflows)* Expose the Geant4 seed as a jsonnet argument
- *(sources)* Add an optional seed to the generator sources
- *(workflows)* Pin source seeds where output is compared

### Bug fixes

- *(deps)* Add eigen as explicit dependency
- *(env)* Export GXSHAREDIR so gmex finds vis-attribute JSONs
- Use std::jthread for generator/master threads
- Correct and share the Philox counter-based RNG
- Widen PDG validation histogram to cover baryons
- Unroll install-hooks loop so deno_task_shell can parse it
- Publish provider products as interface types
- Error on Pythia8MTSource exhaustion
- *(geant4)* Report primaries skipped in build_primaries
- *(file_source)* Reject a negative skip in the constructor
- Guard non-positive POT, drop dead TFile.Open zombie checks
- Retry Pythia8 generation, fail hard after repeated errors
- Work around TGeo GDML import of colliding element names
- *(gdml)* Harden GDML tooling parsing and export checks
- Include averaged-A nuclides in the target list
- Clean geometry stores on the master thread at shutdown
- *(geant4)* Validate export_gdml path before the run manager
- Create production-cut G4Regions on the geometry thread
- Error on production-cut region pattern matching no volumes
- *(bench)* Pin single-threaded recipes to -j 1
- *(workflows)* Match production-cut regions to GeoModel volume names
- *(geant4)* Make a failed master init sticky instead of retrying
- *(geant4)* Clean geometry stores whenever the run manager exists
- *(lint)* Align unit fences with the documented policy
- *(geant4)* Fail cleanly on unknown physics lists, count worker kernels
- *(output)* Bound RNTuple writer memory in event and thread count
- *(output)* Scale the fill-context pool down to the thread count
- *(output)* Block on the fill-context pool to bound events in flight
- *(geant4)* Accept the full uint32 seed range, narrow determinism claim
- *(sources)* Reject invalid pythia_seed headroom

### Refactor

- Share Pythia helpers; guard null data, remap motherId
- Share provider registration and vector magnitude
- Publish constant providers on the job layer
- *(workflows)* Share config blocks via lib.libsonnet
- *(workflows)* Share WorldField field via lib.libsonnet
- Identify failing sub-generator in retry errors
- *(geant4)* Resolve the seed via the shared helper

### Documentation

- Link Doxygen API reference from README
- Propose event-level neutrino flux ntuple schema
- Record 2018 production sample POT table
- *(geant4)* Note that store-cleanup order is immaterial
- Document prek hooks in CONTRIBUTING
- Drop jemalloc from the benchmark comparison
- Record reserve-fix A/B and overhead attribution
- Extend the reproducibility notes to the generator sources

### Performance

- Bind RNTuple inputs directly; RAII for G4Event
- Read only the needed cbmsim branches in the flux converter
- Free GeoModel tree after conversion to reclaim memory
- *(geant4)* Retain hit-buffer capacity across events
- *(geant4)* Use initial-exec TLS for the plugin

### Styling

- Clang-format
- Allow C++17 headers in cpplint, use std::filesystem

### Testing

- Add determinism check; ignore profiling artifacts
- Cover seeded reproduction and the unseeded random draw

### Miscellaneous

- Update pixi lock file
- Update pixi lock file
- Lint with prek via pixi
- Enable Renovate via shared preset
- Update pixi lock file
- Update pull request template
- Add release workflow and shared-config sync
- Add physics smoke test with metrics tracking
- *(bench)* Add performance measurement protocol

### Build

- Collapse plugin boilerplate into aegir_add_plugin()
- Depend on field-service sub-packages, refresh to build 1
- Fail configure on unusable pythia8-config output
- Require the released shipgeometryservice >=0.3
- Refresh pixi.lock for shipgeometryservice 0.3.0
- Require shipdatamodel >=0.2 for the SHiPUnits target
- *(geant4)* Gate initial-exec TLS behind an option
## [0.2.0] - 2026-06-18

### Features

- Resolve geometry DB path via SHIPGEOMETRY_ROOT
- *(ci)* Add pixi-based build and CI workflow
- *(bench)* Geant4 tracing, flamegraph, and saturated sweep tooling
- *(field)* Integrate SHiPFieldService magnetic field service

### Bug fixes

- *(workflows)* Use bare db_file name in remaining geomodel workflows
- Improve SHIPGEOMETRY_ROOT path resolution
- *(deps)* Use libjsonnet instead of broken jsonnet CLI package
- *(trace)* Emit kernel TIDs and thread-name metadata
- *(field)* Error on unmatched volume_pattern; drop unused field decl
- *(deps)* Pin root_cxx_standard==23 to match aegir's C++ standard

### Refactor

- *(sim_output)* Switch validation I/O to ROOT 7 RFile + RHist
- *(workflows)* Parameterise gun_st_geomodel event count via extVar

### Documentation

- *(readme)* Expand pixi usage guide
- Contributing — point at pixi for the dev environment

### Styling

- Pre-commit fixes
- Pre-commit fixes

### Testing

- *(field)* Add smoke workflows exercising covfie + per-region wiring

### Miscellaneous

- Add LICENSE.md symlink for GitHub detection
- Use copy instead of symlink for LICENSE.md
- *(docs)* Fix link to phlex in readme
- Use ShipSoft/.github reusable workflows
- Update pixi lock file
- Add project information to CMake
## [0.1.0] - 2026-05-06
