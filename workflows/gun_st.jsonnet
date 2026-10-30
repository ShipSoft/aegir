local lib = import 'lib.libsonnet';
function(events=100) {
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    gun: lib.gun,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('gun_st_output.root', 'gun_st_validation.root', events),
  },
}
