// EventCalc decays with no Geant4 tracking — writes only the mc_particles and
// event_header records, so a run costs seconds. Use it to check that decay
// vertices land inside the decay vessel before paying for a full simulation.
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    eventcalc: lib.eventcalc { file: std.extVar('infile') },
  },
  modules: {
    output: lib.mc_only_output(std.extVar('simout'), std.extVar('histo')),
  },
}
