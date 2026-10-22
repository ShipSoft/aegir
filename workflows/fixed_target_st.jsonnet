local lib = import 'lib.libsonnet';
function(events=100) {
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    pythia8: lib.fixed_target,
  },
  modules: {
    geant4: lib.geant4_crossing,
    output: lib.full_output('fixed_target_output.root', 'fixed_target_validation.root', events) {
      filter_empty: true,
    },
  },
}
