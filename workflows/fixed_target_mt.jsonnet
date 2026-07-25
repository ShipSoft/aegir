local lib = import 'lib.libsonnet';
// Pass --tla-code seed=N (e.g. a batch job id) for reproducible large-scale
// runs — it seeds both the generator source and Geant4; without it each
// draws its own random seed.
function(seed=null) {
  driver: lib.driver(100000),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    pythia8: lib.fixed_target { [if seed != null then 'seed']: seed },
  },
  modules: {
    geant4: lib.geant4_crossing {
      energy_cut_threshold: 30.0,
      concurrency: 4,
      [if seed != null then 'seed']: seed,
    },
    output: lib.full_output('fixed_target_mt_output.root', 'fixed_target_mt_validation.root') {
      filter_empty: true,
    },
  },
}
