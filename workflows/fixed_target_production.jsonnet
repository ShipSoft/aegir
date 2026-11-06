// Fixed-target production with FairShip's scoring plane behind the hadron
// absorber (FairShip's run_fixedTarget.py): Pythia8 p-W interactions in the
// GeoModel target, tracked through the target and hadron absorber. Every
// particle reaching the plane with at least `ecut` kinetic energy — muons,
// neutrinos and all others — is recorded there as a SimHit with detector_id
// 100 and geometry_node_id 1, and stopped. Tracks below `ecut` are killed
// wherever they are, as in FairShip; events without a plane hit are not
// written. Of the tracked particles, only those reaching the plane and their
// ancestors are written, as FairShip's stack keeps them, so every parent id
// in the output resolves. The plane's tracks are also written, as they
// entered the plane and with their full ancestry, as plane_particles;
// workflows/plane_replay.jsonnet simulates them onwards from there.
//
// As with FairShip's default (no --AddMuonShieldField), the absorber is not
// magnetised; merge a lib.field_map in as `field` to add its field.
//
// Top-level arguments (--tla-code name=value):
//   seed    seeds the generator and Geant4, for reproducible batch jobs;
//           without it each run draws its own (logged at startup)
//   ecut    energy cut in GeV (FairShip's --ecut, default 0.5)
//   events  number of proton interactions
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
function(seed=null, ecut=0.5, events=100000) {
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    // Only the scoring plane is instrumented.
    geometry: lib.geomodel_geometry { sensitive_volumes: [] },
    pythia8: lib.fixed_target { [if seed != null then 'seed']: seed },
  },
  modules: {
    // The energy cut kills tracks below ke_threshold (ecut).
    geant4: lib.geant4_crossing {
      ke_threshold: ecut,
      // Record every track, so that all ancestors of the plane's tracks are
      // there to keep.
      particle_ke_cut: 0.0,
      keep_particles: 'hit_ancestry',
      scoring_planes: [lib.exit_hadron_absorber_plane { ke_threshold: ecut }],
      [if seed != null then 'seed']: seed,
    },
    output: lib.full_output('fixed_target_production.root', 'fixed_target_production_validation.root') {
      filter_empty: true,
    },
  },
}
