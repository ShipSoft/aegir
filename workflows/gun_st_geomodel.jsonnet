local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  resources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
  },
  sources: {
    gun: lib.gun,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('geomodel_output.root', 'geomodel_validation.root'),
  },
}
