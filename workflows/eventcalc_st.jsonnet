// EventCalc LLP decays through the SHiP geometry.
//
// GeoModel geometry, not the builtin one: EventCalc places decay vertices at
// z = 32-82 m from the target, well outside the builtin world volume
// (half-length 20 m in z).
//
// EventCalc measures vertices from the centre of the target; the SHiP GeoModel
// geometry measures from the front face of the first target slab. In
// shipgeometry 0.2.1 the slabs span z = 0-1586.4 mm, so vertices shift
// downstream by half of that. Re-derive offset_z whenever the shipgeometry
// version changes (the 33-disk target on Geometry main spans 0-1460 mm, which
// would give 730 mm). See docs/eventcalc.md, "Geometry origin".
//
//   n=$(pixi run count_eventcalc_events <file>_data.dat)
//   jsonnet --ext-str events=$n --ext-str infile=<file>_data.dat \
//       --ext-str simout=sim.root --ext-str histo=val.root \
//       workflows/eventcalc_st.jsonnet
local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    eventcalc: lib.eventcalc {
      file: std.extVar('infile'),
      offset_z: 793.2,  // mm, target centre in the shipgeometry 0.2.1 frame
    },
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output(std.extVar('simout'), std.extVar('histo')),
  },
}
