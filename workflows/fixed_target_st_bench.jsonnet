// Single-threaded benchmark of the fixed-target production: the Geant4
// settings of fixed_target_production.jsonnet (killing scoring plane behind
// the hadron absorber) with a fixed seed and no output, for comparison with
// FairShip's run_fixedTarget.py. Run with `phlex -j 1`; see
// docs/benchmarks.md.
//
//   jsonnet --ext-str events=420 [--tla-code ecut=5]
//       [--tla-code pre_equilibrium=false] fixed_target_st_bench.jsonnet
//
// ecut and pre_equilibrium are passed on to fixed_target_production.jsonnet
// (defaults 0.5 and true).
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
function(ecut=0.5, pre_equilibrium=true)
  (import 'fixed_target_production.jsonnet')(
    seed=20260703,
    ecut=ecut,
    events=std.parseInt(std.extVar('events')),
    pre_equilibrium=pre_equilibrium,
  ) {
    modules+: {
      output: lib.noop_output,
    },
  }
