local lib = import 'lib.libsonnet';
local events = std.parseInt(std.extVar('events'));
{
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    gun: lib.gun,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('geomodel_output.root', 'geomodel_validation.root', events),
  },
}
