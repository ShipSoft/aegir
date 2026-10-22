local lib = import 'lib.libsonnet';
local events = std.parseInt(std.extVar('events'));
{
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    geometry: lib.builtin_geometry,
    gun: lib.gun { seed: 20260703 },
  },
  modules: {
    // Pinned seed: scripts/check_determinism.sh compares repeated runs of
    // this workflow bitwise, and the perf baselines in docs assume a fixed
    // workload.
    geant4: lib.geant4 { seed: 20260703 },
    output: lib.full_output('bench_gun_st_output.root', 'bench_gun_st_validation.root', events),
  },
}
