// EventCalc LLP decays through the full GeoModel SHiP geometry.
// Check the vertex distribution with eventcalc_only.jsonnet first: if the
// EventCalc frame and the GeoModel frame disagree, this runs cleanly and
// produces no hits. Correct with offset_x/offset_y/offset_z on the source.
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry {
      // Index in this list becomes SimHit::detectorId, so the order is part of
      // the output format. Slots 0-4 follow SHiP::detector_id; 5-7 are
      // overflow, because the upstream tagger and the calorimeter are each
      // built from differently-named volumes with no common substring, and a
      // pattern carries only one id. Remap in analysis:
      //   UpstreamTagger = {0, 5}   Calorimeter = {3, 6, 7}
      sensitive_volumes: [
        '/SHiP/upstream_tagger/coarse_tile',  // 0 UBT, big tiles
        '/SHiP/decay_volume/sbt/sensors',     // 1 SBT liquid scintillator
        '/SHiP/trackers/straw_gas',           // 2 straw gas, not straw_wall
        'HPL_FiberCoreLog',                   // 3 ECAL HPL fibre cores
        'TimDetBar',                          // 4 timing detector
        '/SHiP/upstream_tagger/fine_tile',    // 5 UBT, small tiles
        '/SHiP/calorimeter/wide_pvt',         // 6 ECAL wide PVT layers
        '/SHiP/calorimeter/thin_ps',          // 7 ECAL thin PS layers
      ],
    },
    eventcalc: lib.eventcalc { file: std.extVar('infile') },
  },
  modules: {
    geant4: lib.geant4 { sd_mode: 'merged' },
    output: lib.full_output(std.extVar('simout'), std.extVar('histo')),
  },
}
