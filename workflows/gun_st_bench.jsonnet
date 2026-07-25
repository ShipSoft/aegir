local lib = import 'lib.libsonnet';
{
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    gun: lib.gun { seed: 20260703 },
  },
  modules: {
    geant4: lib.geant4 { seed: 20260703 },  // fixed workload for benchmarks
    output: lib.noop_output,
  },
}
