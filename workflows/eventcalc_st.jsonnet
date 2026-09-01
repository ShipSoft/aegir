// EventCalc LLP decays through the SHiP geometry.
//
// GeoModel geometry, not the builtin one: EventCalc places decay vertices at
// z = 35-80 m, well outside the builtin world volume (half-length 20 m in z).
//
//   n=$(pixi run count_eventcalc_events <file>_data.dat)
//   jsonnet --ext-str events=$n --ext-str infile=<file>_data.dat \
//       --ext-str simout=sim.root --ext-str histo=val.root \
//       workflows/eventcalc_st.jsonnet
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    eventcalc: lib.eventcalc { file: std.extVar('infile') },
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output(std.extVar('simout'), std.extVar('histo')),
  },
}
