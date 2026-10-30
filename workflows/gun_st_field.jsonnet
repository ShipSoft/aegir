// Particle gun through the SHiP geometry with the muon shield and spectrometer
// field maps. The maps and their placements come from the ship-field-maps
// package: field_placements() reads its placements.json from
// $SHIPFIELD_ROOT/share/field/, which records the shipgeometry version the
// translations match.
local lib = import 'lib.libsonnet';
function(events=100) {
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    geometry: lib.geomodel_geometry,
    field: lib.field_placements(),
    gun: lib.gun,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('gun_st_field_output.root', 'gun_st_field_validation.root', events),
  },
}
