// tutorial_muon_beam.jsonnet — a first aegir simulation, step by step.
//
// What this job does
// ------------------
// Fires muons, all with the same momentum, along the z axis. They start a
// little to the side of the axis and pass through a simple test setup in
// Geant4. The muons and the hits they leave are written to a ROOT file.
//
// Run it from the repository root with
//
//   pixi run phlex -c workflows/tutorial_muon_beam.jsonnet
//
// A workflow file is written in jsonnet, a small language that produces JSON.
// To see the plain JSON that phlex actually reads, run
//
//   jsonnet workflows/tutorial_muon_beam.jsonnet
//
// The other workflows in this directory build their jobs from the reusable
// blocks in lib.libsonnet. This one spells every block out so that you can
// see each setting. The comments say which lib.libsonnet block you would
// normally use instead.

// ── Settings ───────────────────────────────────────────────────────────────
// Change these values to try things out. Numbers in a workflow carry no
// units; each setting is read in aegir's standard units: lengths in mm,
// momenta in GeV/c, angles in rad (see docs/units.md).

local n_events = 100;  // number of events; each event holds one muon
local pdg = 13;  // particle type as a PDG code: 13 = μ−, −13 = μ+
local momentum = 50.0;  // GeV/c, the same for every muon

// Where the muons start. The beam axis is the z axis, so x and y say how
// far off the axis the muons are. Every muon starts at the same point.
local offset_x = 20.0;  // mm
local offset_y = 0.0;  // mm
local start_z = -1000.0;  // mm, 0.5 m in front of the target

// The random-number seed. With a fixed seed, everyone who runs this file
// gets exactly the same events. Remove the `seed` lines below and each run
// picks a new seed instead (it is printed at start-up, so you can still
// repeat a run later).
local seed = 12345;

{
  // Every phlex job names its stage. The stage name is attached to all the
  // data the job produces.
  stage: 'simulation',

  // ── Driver ─────────────────────────────────────────────────────────────
  // The driver sets how many events to process. Here one event is one muon,
  // so n_events muons are simulated in total.
  // (lib.libsonnet: lib.driver(n_events))
  driver: {
    cpp: 'generate_layers',
    layers: {
      event: { total: n_events },
    },
  },

  // ── Sources ────────────────────────────────────────────────────────────
  // Sources provide the inputs to the job: the magnetic field, the detector
  // geometry and the particles to simulate. The `cpp` key names the plugin
  // that does the work; the other keys are that plugin's settings.
  sources: {
    // No magnetic field, so the muons travel in straight lines.
    // (lib.libsonnet: lib.null_field. See "Things to try" at the end for
    // switching a field on.)
    field: { cpp: 'field_null_provider' },

    // A small built-in test setup, so that no geometry file is needed:
    //   - a tungsten block 100 mm × 100 mm across (x and y from −50 to
    //     +50 mm), running from z = −500 mm to +500 mm;
    //   - five silicon detector planes, 4 m × 4 m, at z = 2, 4, 6, 8, 10 m;
    //   - all of it in air.
    // With offset_x = 20 mm the muons go through the tungsten. Make the
    // offset larger than 50 mm and they miss it.
    // (lib.libsonnet: lib.builtin_geometry. For the full SHiP detector, see
    // lib.geomodel_geometry.)
    geometry: { cpp: 'geometry_builtin_provider' },

    // The particle gun creates one particle per event.
    // (lib.libsonnet: lib.pencil_gun is a similar, ready-made beam.)
    gun: {
      cpp: 'particle_gun_source',
      // The gun labels its muons with the stage name itself, so it needs
      // its own copy of the setting from the top of the file.
      stage: 'simulation',
      pdg: pdg,
      // The momentum is drawn between p_min and p_max. Making them equal
      // gives every muon the same momentum.
      p_min: momentum,  // GeV/c
      p_max: momentum,  // GeV/c
      // Each muon's direction is drawn at a random angle of up to
      // max_theta from the z axis. With 0 every muon goes straight
      // down +z.
      max_theta: 0.0,  // rad
      vertex_x: offset_x,  // mm
      vertex_y: offset_y,  // mm
      vertex_z: start_z,  // mm
      seed: seed,
    },
  },

  // ── Modules ────────────────────────────────────────────────────────────
  // Modules process each event: here Geant4 tracks the muons through the
  // geometry, and the output module writes the results to file.
  modules: {
    // Geant4 with the FTFP_BERT physics list, its standard choice for
    // high-energy physics. Raise `verbosity` to see what Geant4 is doing.
    // (lib.libsonnet: lib.geant4)
    geant4: {
      cpp: 'geant4_module',
      physics_list: 'FTFP_BERT',
      verbosity: 0,
      seed: seed,
    },

    // Writes two files:
    //   - tutorial_muon_beam.root: the muons and the detector hits, one
    //     entry per event;
    //   - tutorial_muon_beam_validation.root: summary histograms for a
    //     quick look.
    // Set `mode: 'mc_only'` to keep only the generated muons, or
    // `mode: 'noop'` (and drop the file names) to write nothing at all.
    // (lib.libsonnet: lib.full_output(rntuple_file, histo_file))
    output: {
      cpp: 'sim_output_module',
      mode: 'full',
      rntuple_file: 'tutorial_muon_beam.root',
      histo_file: 'tutorial_muon_beam_validation.root',
    },
  },
}

// ── Things to try ──────────────────────────────────────────────────────────
//   - Change `momentum` and compare how far the muons scatter in the
//     tungsten.
//   - Set `pdg = -13` to fire μ+ instead.
//   - Give `max_theta` a small value, e.g. 0.01 rad, to spread the muons
//     out into a cone.
//   - Move the start point with `offset_x` and `offset_y`; above 50 mm the
//     muons miss the tungsten block.
//   - Replace the field source with lib.world_field_05T_y, a uniform
//     0.5 T field along y (add `local lib = import 'lib.libsonnet';` at the
//     top), and watch the muons bend in x. μ− and μ+ bend opposite ways.
//   - Raise `n_events` for more statistics.
