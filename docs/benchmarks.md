# Benchmark results

Measured on a single machine (i7-8665U: 4 cores / 8 threads).
All results use `hyperfine` with warmup runs.
Phlex v0.1.0, ROOT 6.38, Pythia8 8.317, Geant4 11.4.
The bench workflows pin both the generator-source and geant4 `seed`, so
every run simulates the same fixed workload.

## Pythia8: standalone vs Phlex

10,000 events, `SoftQCD:inelastic`, 400 GeV fixed-target p-p.
Standalone benchmark uses single-threaded `Pythia8::Pythia`.

| Command | Mean | Relative |
|:---|---:|---:|
| standalone | 6.37 ± 0.55 s | 1.00 |
| phlex noop | 6.57 ± 0.35 s | 1.03 ± 0.10 |
| phlex mc_only | 9.81 ± 1.30 s | 1.54 ± 0.24 |

Findings:
- Phlex framework overhead (scheduling + data-product transport) is ~3% — negligible.
- RNTuple serialisation + histogramming adds ~50% wall time, dominated by I/O syscalls
  (~5 s of system time in `mc_only` vs ~0 in `noop`).
- The standalone benchmark includes Pythia8 init (~1.3 s) in both standalone and Phlex
  runs, so per-event overhead is even smaller than the 3% headline.

## Pythia8 event count scaling

Measured with standalone benchmark and Phlex noop to isolate startup costs.

| Events | Standalone | Phlex noop | Phlex overhead |
|---:|---:|---:|---:|
| 1 | — | 1.35 s | ~1.35 s (all startup) |
| 100 | ~0.02 s + init | 1.37 s | startup-dominated |
| 1,000 | ~0.12 s + init | 1.52 s | startup-dominated |
| 10,000 | ~1.1 s + init | 2.97 s | ~0.05 ms/event extra |
| 100,000 | ~15.3 s + init | 23.7 s | ~0.07 ms/event extra |

Both standalone and Phlex include Pythia8 init (~1.3 s). The Phlex-specific
startup is ~0.2 s (graph construction + plugin loading). Per-event framework
overhead is 0.05–0.07 ms, compared to ~0.12 ms/event for Pythia8 generation.

## Geant4 direct integration (current)

Since the rework of the Geant4 module (direct per-thread worker kernels,
no event queue), the sections below this one describing "threading
backends" and the queue/promise pattern are **historical** — those
backends no longer exist. Current numbers, measured on a different
machine (i5-1334U: 2 P-cores + 8 E-cores, 12 threads), 2000 events,
particle gun (muons, 10–100 GeV), FTFP_BERT, noop output. These runs
predate the switch of the default physics list to FTFP_BERT_HP_EMZ (with
γ → μμ and e⁺e⁻ → μμ), which costs more per event:

| Config | time |
|:---|---:|
| g4=4, `-j 4` | 5.58 ± 0.31 s |
| g4=8, `-j 8` | 4.72 ± 0.16 s |
| g4=12, `-j 12` | 4.19 ± 0.17 s |

Findings:

- Throughput keeps improving up to the full hardware thread count on
  this machine — the earlier "plateau at physical cores" conclusion was
  an artefact of the queue-based backend.
- Voluntary context switches are ~2 per event, independent of thread
  count — the phlex event-loop driver hands each event index across a
  dedicated driver thread (two semaphore wake/wait pairs per event).
- The module's `concurrency` must not exceed phlex `-j`: the framework
  schedules at most `-j` simulate calls concurrently, so extra worker
  slots only waste initialisation. The module now defaults its
  `concurrency` to the framework parallelism and warns when the
  configured value exceeds it. Running `-j 1` with `concurrency: 4`
  used to cost 2× in wall time.

Raw data, keyed by git SHA: `results/` (protocol:
`scripts/bench_baseline.sh`).

## Fixed target on one thread, compared with FairShip

`workflows/fixed_target_production.jsonnet` against FairShip's
`run_fixedTarget.py -e 0.5` (FairShip c29d044f3, Geant4 11.4, both with
FTFP_BERT_HP_EMZ and a 0.5 GeV energy cut), on an i5-1334U with the job
pinned to one performance core (`taskset -c 0`, `phlex -j 1`). CPU time per
event is the difference between a 420-event and a 20-event run divided by
400, which removes the roughly 30 s of initialisation both programs need:

| | CPU time per event |
|:---|---:|
| aegir 72b8868, energy cut applied after the first step | 0.47 s |
| FairShip | 0.17 s |
| aegir 844f446, sub-threshold secondaries dropped before tracking | 0.14 s |

Raw data for the aegir rows (20- and 420-event runs), keyed by git SHA:
`results/fixed_target_<sha>.json` (protocol: `just bench-fixed-target`,
run under `taskset -c 0`). A run from a tree with uncommitted changes is
saved as `fixed_target_<sha>-dirty.*`, so it can't overwrite the results
for the commit itself.

The energy cut used to act only at the end of a step. Every secondary
created below the threshold therefore still took one step, and what that
step produced took one step in turn, so most of the showers in the target
were still simulated. For 50 GeV pions in tungsten, 99% of the tracked
secondaries were of this kind. They are now dropped when Geant4 stacks
them, before any step, as FairShip does in `PreTrack`.

The two programs do not do identical work, so read the remaining difference
with care: FairShip simulates the field in the hadron absorber by default
and builds only the cave, target and absorber, while aegir runs without a
field in the full detector geometry. FairShip's generator also leaves out
primaries below the cut or going backwards, and follows tracks that fall
below the cut on the way, which aegir stops.

To repeat the aegir side: `just bench-fixed-target` (needs the GeoModel
geometry DB).

### Without the pre-equilibrium stage

After that change, more than half of the time goes into the
pre-equilibrium stage of nuclear de-excitation, which follows each hadronic
interaction and emits low-energy nucleons and light ions.
`pre_equilibrium: false` skips it and hands the excited nucleus straight to
evaporation. This is not what FairShip simulates, so it is off unless asked
for.

Time per event on a different machine (AMD EPYC-Genoa virtual machine, one
pinned core, wall time, 1020 minus 20 events, three repeats that agree to
within 0.6 ms):

| Energy cut | default | `pre_equilibrium: false` | Speed-up |
|---:|---:|---:|---:|
| 0.5 GeV | 114 ms | 49 ms | 2.3× |
| 5 GeV | 63 ms | 8.3 ms | 7.6× |
| 10 GeV | 37 ms | 5.4 ms | 6.9× |
| 20 GeV | 22 ms | 4.1 ms | 5.4× |

On the same machine, with 420 minus 20 events and a 0.5 GeV cut, FairShip
took 0.13 s per event and aegir with the energy cut applied after the first
step 0.33 s. The benchmark workflow takes the cut and the switch as
top-level arguments:

```bash
phlex -j 1 -c <(jsonnet --ext-str events=1020 --tla-code ecut=5 \
    --tla-code pre_equilibrium=false workflows/fixed_target_st_bench.jsonnet)
```

Particles reaching the plane behind the hadron absorber above the cut, from
batch runs of `fixed_target_production.jsonnet` with the same Pythia seeds
for both settings: 200 000 events per setting at 0.5 GeV and 1 000 000 at
5, 10 and 20 GeV. The difference is `false` relative to the default, with
its uncertainty taken from the spread between seeds. Species with fewer
than 20 particles are left out.

| Cut | Particle | default | `pre_equilibrium: false` | Difference |
|---:|:---|---:|---:|---:|
| 0.5 GeV | neutrinos | 4472 | 4435 | −0.8 ± 1.9% |
| | neutrons | 885 | 892 | +0.8 ± 4.2% |
| | muons | 283 | 286 | +1.1 ± 5.9% |
| | protons | 64 | 69 | +8 ± 15% |
| | charged pions | 53 | 29 | −45 ± 15% |
| 5 GeV | neutrinos | 773 | 670 | −13.3 ± 4.5% |
| | muons | 575 | 573 | −0.3 ± 4.2% |
| 10 GeV | neutrinos | 188 | 193 | +3 ± 9% |
| | muons | 323 | 310 | −4.0 ± 4.9% |
| 20 GeV | neutrinos | 53 | 51 | −4 ± 15% |
| | muons | 146 | 135 | −7.5 ± 7.4% |

Muons agree at every cut. Two differences reach about 3 standard
deviations: about half as many charged pions at 0.5 GeV, all of them below
2 GeV/c, and 13% fewer neutrinos at 5 GeV, mostly between 5 and 10 GeV/c.
The neutrinos above 5 GeV/c in the 0.5 GeV sample point the same way
(0.91 ± 0.11) but are too few to confirm it. Two such differences among a
dozen comparisons are unlikely to both be chance. An earlier comparison, on
a first version of the stacking-action change, showed 15% more neutrons
without the stage; these runs do not reproduce that.

## Geant4: single-threaded vs multi-threaded (historical)

Particle gun (muons, 10–100 GeV), FTFP_BERT, noop output, `mt` backend.

At 500 events:

| Command | Mean | Relative |
|:---|---:|---:|
| G4 single-threaded | 3.20 ± 0.09 s | 1.00 |
| G4 multi-threaded (4T) | 5.52 ± 0.08 s | 1.73 |

At 100 events, ST and MT 4T are within noise (~3.2 s each) — G4 initialisation
(~2 s) dominates. At 500 events, MT overtakes ST significantly (see thread
scaling below).

## Geant4 MT thread scaling (historical)

500 events, particle gun (muons, 10–100 GeV), FTFP_BERT, noop output.
Uses `mt` backend (`G4MTRunManager`).

| Threads | Mean | Speedup vs 1T |
|---:|---:|---:|
| 1 | 9.40 ± 1.31 s | 1.00 |
| 2 | 7.63 ± 0.48 s | 1.23x |
| 4 | 5.52 ± 0.08 s | 1.70x |
| 8 | 5.90 ± 0.49 s | 1.59x |

Positive scaling up to 4T (= physical core count), then a plateau at 8T.
No deadlocks at any thread count. See [per-event timing
analysis](#per-event-timing-analysis) for why scaling plateaus.

## Geant4 threading backend comparison (historical)

500 events, 4 threads, particle gun, FTFP_BERT, noop output.

| Backend | Mean | vs ST |
|:---|---:|---:|
| Single-threaded | 3.20 ± 0.09 s | 1.00 |
| `mt` (G4MTRunManager) | 5.27 ± 1.22 s | 1.65x slower |
| `task_ptl` (PTL pool) | 5.77 ± 0.79 s | 1.80x slower |
| `task_tbb` (TBB arena) | 7.74 ± 3.22 s | 2.42x slower |

At 500 events, fixed cost (G4 init ~2 s) still dominates. At 2000 events
with a full thread sweep, `mt` and `task_ptl` converge to within noise
(26.1 s vs 26.3 s at 4T, 10 runs). Both plateau at 4T with no benefit
beyond physical core count. `task_tbb` is consistently slowest due to TBB
arena contention and is the only backend that can deadlock at high thread
counts.

`mt` is the default — it is the most battle-tested G4 MT model and avoids
all TBB arena interactions.

## Per-event timing analysis (historical, queue-based backend)

Per-event timing data from the `mt` backend at 4T, 200 events, particle gun.
All times in milliseconds.

### Thread scaling effect on per-event G4 processing time

| Threads | g4_process p50 | g4_process p99 | queue_wait p50 |
|---:|---:|---:|---:|
| 1 | 33.1 | 264.5 | 0.0 |
| 2 | 38.4 | 314.1 | 0.0 |
| 4 | 66.6 | 442.1 | 0.1 |
| 8 | 62.9 | 1147.6 | 0.3 |

The pure G4 simulation time (`g4_process`) inflates ~2x from 1T to 4T. This
is CPU cache contention: on a 4-core/8-thread CPU, 4 G4 workers share L1/L2
caches across cores. At 8T, two workers share each physical core via
hyperthreading, further inflating tail latency (p99 = 1.1 s).

Queue wait is negligible at all thread counts (p50 < 1 ms). The queue/promise
pattern is not the bottleneck.

### `max_concurrent` sweep (4T, 500 events)

| max_concurrent | g4_process p50 | queue_wait p50 | round_trip p50 |
|---:|---:|---:|---:|
| 2 | 10.5 | 0.0 | 10.5 |
| 4 | 10.5 | 0.0 | 10.9 |
| 8 | 19.3 | 27.3 | 52.1 |
| 12 | 19.9 | 30.9 | 55.7 |

At mc=2, only 2 of 4 G4 workers are kept busy (workers starve). At mc=4
(= num_threads, the default), all workers are fed and g4_process stays low.
At mc≥8, CPU oversubscription inflates g4_process and queue backpressure
increases. The default `max_concurrent = num_threads` is already optimal.

### Conclusion

The scaling plateau at 4T is caused by **CPU core saturation**, not a
software bottleneck. The queue/promise handoff adds < 1 ms overhead. To
improve throughput further, run on hardware with more physical cores.

## How to run

All benchmarks are defined as `just` recipes. Override defaults with variables:

```sh
just bench-pythia8                         # default: 10k events
just events=100000 bench-pythia8           # override event count
just bench-geant4                          # G4 ST vs MT, 100 events
just g4_events=500 bench-geant4            # more events
just g4_events=500 bench-geant4-threads    # sweep 1,2,4,8 threads
just g4_events=500 bench-geant4-backends   # compare mt/task_ptl/task_tbb
just bench-fixed-target                    # fixed target on one thread, 20 and 420 events
just bench-fairship                        # default vs FairShip config
just bench-scaling                         # event count sweep
```

Results are exported to `bench-*.md` and `bench-*.json` (gitignored).

## Tooling

Activate the bench environment first: `pixi shell -e bench`. It installs
`hyperfine`, `just`, `linux-perf`, and Python.

### Per-event attribution (Chrome trace)

Rebuild with tracing on, then run a small workflow:

```sh
cmake -S . -B build -DAEGIR_ENABLE_TRACE=ON
pixi run build
just profile-trace                           # default: gun_st_bench, 50 events
just trace_workflow=gun_mt_bench trace_events=200 profile-trace
```

`trace_<workflow>.json` is a Chrome Trace Event JSON file. Open in
<https://ui.perfetto.dev> directly. Spans emitted: `init_master`,
`init_worker`, `simulate` (framework round-trip), `build_primaries`,
`ProcessOneEvent` (raw Geant4 tracking), `flush_hits`. Counters
`hits` and `particles` give per-event yields.

Framework overhead per event = `simulate − ProcessOneEvent`. Useful for
quantifying the phlex↔Geant4 integration cost.

### Sample-based CPU profile (perf + flamegraph)

```sh
just profile-flamegraph                                # gun_st_bench, 500 events
just profile_workflow=gun_mt_bench profile-flamegraph
```

Records at 99 Hz with DWARF call-graphs. If `flamegraph.pl` and
`stackcollapse-perf.pl` are on `$PATH` (e.g. via `nix-shell -p
flamegraph`), the recipe writes `flamegraph_<workflow>.svg`. Otherwise
it prints the top of `perf report` to stdout.

### Scaling sweep with per-event percentiles

`scripts/run_benchmark.py` runs a thread × parallelism grid and emits
both aggregate (real time, CPU efficiency, RSS) and per-event
(`simulate`, `ProcessOneEvent`, framework overhead) metrics. Tracing is
required for the per-event columns; without it those columns are blank.

```sh
just bench-sweep                                       # gun_mt, 1000 events
just bench_workflow=fixed_target_mt bench-sweep        # SHiP-realistic
```

The sweep emits `benchmark_<workflow>.csv` and, with `--plot`, a PNG
with four panels (real time vs threads, CPU efficiency vs parallelism,
framework overhead vs threads, speedup vs baseline).

### Saturated-throughput sweep

A 1-thread measurement on an idle machine sees no cache or
memory-bandwidth contention from siblings, so the resulting number
overestimates what one slot delivers when the host is fully loaded.
`--saturate` fixes that: for each thread count `T`, it launches
`floor(num_cores / T)` parallel phlex processes, each pinned to a
disjoint `T`-wide CPU slice via `taskset`, so every row of the sweep
runs against a fully-loaded host.

```sh
just bench-sweep-saturated                             # default sweep
```

The extra `copies` and `cohort_wall_s` columns let you compute
per-process throughput (`num_events / real_time_s` per copy) and
cohort throughput (`copies * num_events / cohort_wall_s`). The gap
between the idle-baseline 1T row and the saturated 1T row is the
contention cost — typically 20–50 % on a busy server.

## Memory tuning

Three things determine how much memory a run needs beyond the geometry
and Geant4 themselves:

- **Thread count.** phlex sizes its thread pool to the machine's core
  count unless you pass `-j`. On a batch node this is rarely what you
  want: a job allocated a single CPU slot on a 56-core machine would
  still start 56 threads and oversubscribe its CPU. Always pass `-j`
  matching the CPUs the job actually has.
- **Output buffering.** The RNTuple writer keeps a fixed pool of fill
  contexts (`fill_contexts`, default 4 or the thread count if lower),
  each buffering events in memory until it has roughly
  `cluster_size_mib` (default 32) of compressed data to write out. The
  writer's own memory use is therefore about
  `fill_contexts × 3–4 × cluster_size_mib` regardless of how many
  events the job processes — with the defaults, roughly half a GB.
  Both keys can be set in the `sim_output_module` block of the
  workflow.
- **Events in flight.** Every event that has been simulated but not
  yet written also holds its data in memory. phlex itself does not
  limit how many events are in flight, so the writer throttles the
  framework instead: when all fill contexts are busy, further write
  calls block their threads until a context frees up, which stalls
  event production. At most about one unwritten event per thread
  exists at a time, so this contribution is bounded by `-j` times the
  in-memory size of an event — another reason to keep `-j` matched to
  the CPUs the job actually has rather than letting it default to the
  core count.

Before these bounds existed, writer memory grew with both the thread
count and the number of events (issue #77): a 3,200-event job on a
56-core node reached ~73 GB. The same job now stays within a few GB.
