// Fixed-target events with the P42 beam spot: Pythia8 proton-tungsten
// interactions generated on the beam axis, moved by the beam_spot transform
// to where the beam hits the target (with dilution, the 4 Hz sweep), then
// tracked through the GeoModel geometry. See docs/beam_spot.md.
//
// Top-level arguments (--tla-code / --tla-str):
//   events  number of events (default 100)
//   beam    'sps' or 'gauss', the beam distribution of the beam-line
//           simulation (default 'sps')
//   dilution  sweep the beam over the target (default true)
//   seed    seeds the generator, the beam spot and Geant4; without it each
//           run draws its own (logged at startup)
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
function(events=100, beam='sps', dilution=true, seed=null) {
  local spots = {
    sps: if dilution then lib.p42_beam_spot_sps else lib.p42_beam_spot_sps_no_dilution,
    gauss: if dilution then lib.p42_beam_spot_gauss else lib.p42_beam_spot_gauss_no_dilution,
  },
  assert std.objectHas(spots, beam) : "beam must be 'sps' or 'gauss'",
  local seeded(block) = block { [if seed != null then 'seed']: seed },
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    pythia8: seeded(lib.fixed_target),
  },
  modules: {
    beam_spot: seeded(spots[beam]),
    // Track and write the moved particles, not the generator's.
    geant4: seeded(lib.geant4_crossing) { particles_from: 'beam_spot' },
    output: lib.full_output('fixed_target_beam_spot.root',
                            'fixed_target_beam_spot_validation.root') {
      particles_from: 'beam_spot',
    },
  },
}
