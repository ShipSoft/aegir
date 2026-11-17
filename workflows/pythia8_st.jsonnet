local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(100),
  resources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
  },
  sources: {
    pythia8: lib.pythia8,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('pythia8_st_output.root', 'pythia8_st_validation.root'),
  },
}
