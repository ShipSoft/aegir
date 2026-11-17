local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(100),
  resources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
  },
  sources: {
    pythia8: lib.fixed_target,
  },
  modules: {
    geant4: lib.geant4_crossing,
    output: lib.full_output('fixed_target_output.root', 'fixed_target_validation.root') {
      filter_empty: true,
    },
  },
}
