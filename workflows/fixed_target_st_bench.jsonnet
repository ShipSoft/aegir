// Single-threaded benchmark of the fixed-target production: the Geant4
// settings of fixed_target_production.jsonnet (0.5 GeV energy cut, killing
// scoring plane behind the hadron absorber) with a fixed seed and no output,
// for comparison with FairShip's run_fixedTarget.py. Run with `phlex -j 1`;
// see docs/benchmarks.md.
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
local production = (import 'fixed_target_production.jsonnet')(
  seed=20260703, events=std.parseInt(std.extVar('events'))
);
production {
  modules+: {
    output: lib.noop_output,
  },
}
