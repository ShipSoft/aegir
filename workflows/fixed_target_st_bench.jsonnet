// Single-threaded benchmark of the fixed-target production: the Geant4
// settings of fixed_target_production.jsonnet (killing scoring plane behind
// the hadron absorber) with a fixed seed and no output, for comparison with
// FairShip's run_fixedTarget.py. Run with `phlex -j 1`; see
// docs/benchmarks.md.
//
//   jsonnet --ext-str events=420 [--tla-code ecut=5]
//       [--tla-str nuclear_deexcitation=none] fixed_target_st_bench.jsonnet
//
// ecut and nuclear_deexcitation are passed on to
// fixed_target_production.jsonnet (defaults 0.5 and 'full').
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
function(ecut=0.5, nuclear_deexcitation='full')
  (import 'fixed_target_production.jsonnet')(
    seed=20260703,
    ecut=ecut,
    events=std.parseInt(std.extVar('events')),
    nuclear_deexcitation=nuclear_deexcitation,
  ) {
    modules+: {
      output: lib.noop_output,
    },
  }
