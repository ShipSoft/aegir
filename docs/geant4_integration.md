<!--
SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
SPDX-License-Identifier: LGPL-3.0-or-later
-->

# Geant4 integration strategy

aegir uses a **direct worker** integration pattern where Phlex framework
threads act as Geant4 worker threads directly, with no separate G4 thread
pool or event queue. This is inspired by the
[CMSSW OscarMTProducer](https://github.com/cms-sw/cmssw/blob/master/SimG4Core/Application/src/OscarMTProducer.cc)
pattern used in CMS.

## Why direct workers?

We evaluated five integration strategies in the
[ship-phlex-sim](https://github.com/ShipSoft/ship-phlex-sim) benchmarking
repository:

1. **Synchronous single-threaded** -- `G4RunManager::BeamOn(1)` on a
   dedicated thread.
2. **Asynchronous single-threaded** -- dedicated G4 thread with an event
   queue and `std::future` for synchronisation.
3. **Synchronous multi-threaded (slots)** -- `G4TaskRunManager` with
   per-worker rendezvous slots.
4. **Asynchronous multi-threaded (queue)** -- `G4MTRunManager` on a
   dedicated thread; G4 workers pop from a concurrent queue.
5. **Direct workers** -- framework threads are G4 workers; no queue, no
   `BeamOn`.

Strategies 1--4 all involve cross-thread handoff overhead (queues, promises,
condition variables). The direct worker approach eliminates this entirely:
the thread that receives the event from the framework is the same thread
that processes it through Geant4.

In benchmarks with particle gun events on a 4-core machine, the direct
pattern matched the best queue-based strategy (sync MT slots) while being
simpler to configure and reason about. It also removes the `num_events`
configuration footgun present in the queue-based MT module, where the
configured event count had to exactly match the driver's total.

## Architecture

```
Master thread (std::thread)
  |
  +-- Owns G4MTRunManager
  +-- Runs Initialize() + RunInitialization()
  +-- Stores world volume, physics list, detector construction
  +-- Blocks until shutdown

Phlex TBB worker threads (N = concurrency)
  |
  +-- Lazy init: G4WorkerRunManagerKernel per thread
  +-- simulate() builds G4Event from MCParticles
  +-- Calls G4EventManager::ProcessOneEvent()
  +-- Collects results from thread-local storage
```

### Master initialisation

A dedicated `std::thread` creates a `G4MTRunManager` and runs the full
Geant4 initialisation sequence (geometry, physics, run initialisation). It
then stores the world physical volume, physics list, and detector
construction pointers for workers to use. The master thread blocks on a
shutdown future until the module is destroyed.

### Worker initialisation

Each Phlex thread lazily initialises on its first call to `simulate()`:

1. Assigns a unique G4 thread ID via `G4Threading::G4SetThreadId()`
2. Calls `G4WorkerThread::BuildGeometryAndPhysicsVector()`
3. Creates a `G4WorkerRunManagerKernel` and defines the world volume
4. Initialises physics and sensitive detectors for this thread
5. Sets user actions (tracking, energy cut) on the event manager

### Event processing

Instead of `G4RunManager::BeamOn()`, the module:

1. Builds a `G4Event` directly from the input `MCParticle` vector — keeping
   only final-state entries, see [Primary selection](#primary-selection) —
   creating `G4PrimaryVertex` and `G4PrimaryParticle` objects (no
   `G4VUserPrimaryGeneratorAction` involved)
2. Sets the G4 state to `G4State_GeomClosed`
3. Calls `G4EventManager::ProcessOneEvent(event)`
4. Collects hits and particles from thread-local storage

This bypasses the G4 run loop entirely, giving the framework full control
over event scheduling.

### Primary selection

`SHiP::MCParticle::status` carries the **HepMC status code** — the same
convention Pythia8's `Particle::statusHepMC()` produces and EDM4hep uses for
its `generatorStatus`:

| Code | Meaning | Tracked? |
|------|---------|----------|
| 0 | Empty entry, no meaningful information | no |
| 1 | Final state — not decayed *by the generator*; may still be unstable | **yes** |
| 2 | Decayed Standard Model hadron, tau or muon | no |
| 3 | Documentation entry | no |
| 4 | Incoming beam particle | no |
| 11-200 | Intermediate entry, generator-dependent classification | no |

Only status-1 entries become primaries. Note that "final state" does not mean
stable: a final-state `K_S` is status 1 and is expected to decay in the
detector — that is exactly the work being handed over. Everything else is
either the generator's own bookkeeping or a particle the generator has already
decayed, and tracking it alongside the daughters it already produced would
double-count the event.

The predicate lives in `src/hepmc_status.hpp`, next to the code set. Geant4's
own HepMC example uses the same `status == 1` test.

Skipped primaries are counted and reported once per event in an aggregated
warning; they remain in the output `mc_particles` product, so the record is
not lost.

Every generator here pre-filters to final state before writing
`mc_particles`, so today the check is a no-op: it is a guard for an input
that does not, such as a stored file or a generator that emits its whole
record.

Without it the failure mode would be *silent and partial* rather than loud.
Geant4 declines to track short-lived definitions (quarks, gluons, diquarks,
strings) on its own, but it happily tracks decayed hadrons and beam particles
alongside the daughters they already produced.

### Shutdown

The `G4MTRunManager` is intentionally leaked at shutdown. Its destructor
accesses global singletons that may already be torn down during plugin
unloading, causing crashes. This is a known Geant4 limitation.

## Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `physics_list` | string | `FTFP_BERT` | Geant4 reference physics list name |
| `concurrency` | int | `1` | Number of concurrent worker threads |
| `verbosity` | int | `0` | Geant4 run verbosity level |
| `sd_mode` | string | `scoring` | Sensitive detector mode: `scoring` or `crossing` |
| `ke_threshold` | double | `0.0` | Kinetic energy threshold for crossing SD (GeV) |
| `energy_cut` | bool | `false` | Enable stepping energy cut |
| `energy_cut_threshold` | double | `ke_threshold` | KE below which tracks are killed (GeV) |
| `particle_ke_cut` | double | `0.0` | KE below which secondary particles are not recorded (GeV) |
| `track_all_primaries` | bool | `false` | Hand every input `MCParticle` to Geant4 regardless of its generator status. By default only HepMC status-1 (final-state) entries are tracked; set this only for inputs whose `status` does not follow the HepMC convention |
| `regions` | map | `{}` | Volume name pattern to production cut (mm) mapping |
| `export_gdml` | string | *(unset)* | Write the constructed geometry to this GDML file after initialisation. Errors if the file exists. Lets external tools (e.g. the GENIE event generator) use exactly the geometry Geant4 tracks in |
| `progress_interval` | int | `100` | Log a progress line (event count and average rate) every this many simulated events; `0` disables |

For consumers that read the exported file with ROOT's TGeo importer, run
`scripts/gdml_fix_element_names.py` on it first: ROOT confuses materials
and elements that share a name after pointer-suffix stripping (Geant4's
NIST materials routinely do — material "Iron" made of element "Iron")
and silently imports them as empty mixtures. The script renames the
colliding elements; the physics is unchanged.
`scripts/gdml_target_nuclei.py` then lists the nuclide PDG codes of the
geometry — e.g. the target list neutrino cross-section splines must
cover.

Example workflow configuration:

```jsonnet
{
  modules: {
    geant4: {
      cpp: 'geant4_module',
      physics_list: 'FTFP_BERT',
      concurrency: 4,
      sd_mode: 'crossing',
      ke_threshold: 0.5,
      energy_cut: true,
      particle_ke_cut: 1.0,
      regions: { '/SHiP/target': 50, '/SHiP/muon_shield/magn_absorb': 50 },
    },
  },
}
```
