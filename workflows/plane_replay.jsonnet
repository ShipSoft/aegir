// Second stage of a staged simulation: replay the particles a scoring plane
// recorded in an earlier run (e.g. fixed_target_production.jsonnet) and
// simulate them further. Each event of the input becomes one event here.
//
// plane_particles holds every track the plane recorded, as it entered the
// plane (HepMC status 1), plus its ancestry (other statuses, which Geant4
// does not track), so the tracks are simulated from the plane on. The
// geometry must therefore not contain the killing plane, or it would stop
// them at once; this workflow has no scoring planes. The input's event
// header is passed on, with original_event_id set to the earlier run's event
// number.
//
// Top-level arguments (--tla-code / --tla-str):
//   infile  output of the earlier run (--tla-str)
//   events  number of events to replay, at most the entries in infile:
//           python3 scripts/count_entries.py <infile>
//   skip    first entry to replay (default 0)
//   seed    seeds Geant4, for reproducible batch jobs; without it each run
//           draws its own (logged at startup)
//   ecut    energy cut in GeV (default 0.5, as in fixed_target_production)
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
function(infile, events, skip=0, seed=null, ecut=0.5) {
  local job = lib.events(events),
  stage: lib.stage,
  driver: job.driver,
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    input: {
      cpp: 'file_source',
      stage: lib.stage,
      input_file: infile,
      product: 'plane_particles',
      skip: skip,
    },
  },
  modules: {
    geant4: lib.geant4_crossing {
      ke_threshold: ecut,
      [if seed != null then 'seed']: seed,
    },
    output: job.full_output('plane_replay.root', 'plane_replay_validation.root'),
  },
}
