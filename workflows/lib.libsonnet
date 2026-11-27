// lib.libsonnet — building blocks shared by the aegir workflows.
//
// Workflows compose these with jsonnet object merging, e.g.
//   lib.geant4 { concurrency: 4 }
// overrides or extends a block without repeating it.
{
  // Phlex requires every job to name its stage; products are labelled with
  // it. Workflows set `stage: lib.stage` at the top level, and `phlex
  // --stage` overrides it.
  stage:: 'simulation',

  driver(events):: {
    cpp: 'generate_layers',
    layers: {
      event: { total: events },
    },
  },

  // ── sources ────────────────────────────────────────────────────────────
  null_field:: { cpp: 'field_null_provider' },

  // Field maps in the SHiP field-map format. Each magnet names a `file`
  // (bare names resolve via $SHIPFIELD_ROOT/share/field/), the `map` inside
  // it, and optionally a `translation` [x, y, z] in mm placing the map's
  // origin in the global frame.
  field_map(magnets):: {
    cpp: 'field_map_provider',
    magnets: magnets,
  },

  // Magnets, maps and their placements from a placement manifest (field_service
  // docs/placements_format.md). The default is the SHiP muon shield and
  // spectrometer manifest that ship-field-maps installs under
  // $SHIPFIELD_ROOT/share/field/.
  field_placements(manifest='placements.json'):: {
    cpp: 'field_map_provider',
    placements: manifest,
  },

  // 0.5 T constant By over the whole World volume — the field-smoke setup.
  world_field_05T_y:: self.field_map([
    {
      name: 'WorldField',
      volume_pattern: 'World',
      file: 'world_05T_y.root',
      map: 'world_05T_y',
    },
  ]),

  builtin_geometry:: { cpp: 'geometry_builtin_provider' },

  geomodel_geometry:: {
    cpp: 'geometry_geomodel_provider',
    db_file: 'ship_geometry.db',
    sensitive_volumes: ['ScoringPlane'],
  },

  // The generator sources follow the same seeding convention as the geant4
  // module below: without a `seed`, each run draws a random one (logged at
  // startup, so a run can be reproduced after the fact). Merge one in for
  // reproducible primaries, e.g. lib.gun { seed: 12345 }.
  // Default spray gun: 10–100 GeV μ− in a narrow cone from z = −500 mm.
  gun:: {
    cpp: 'particle_gun_source',
    pdg: 13,
    p_min: 10.0,  // GeV/c
    p_max: 100.0,  // GeV/c
    max_theta: 0.1,  // rad
    vertex_z: -500.0,  // mm
  },

  // Pencil beam for the field smokes: straight 20 GeV μ− from z = −2 m.
  pencil_gun:: {
    cpp: 'particle_gun_source',
    pdg: 13,
    p_min: 20.0,  // GeV/c
    p_max: 20.0,  // GeV/c
    max_theta: 0.0,  // rad
    vertex_z: -2000.0,  // mm
  },

  // `record` selects how much of the generator's event record is emitted:
  // 'final_state' (only what Geant4 will track) or 'full' (the whole record,
  // with HepMC statuses and mother links intact — several times larger; the
  // geant4 module still tracks only status-1 entries). See
  // docs/generator_record.md.
  pythia8:: {
    cpp: 'pythia8_source',
    beam_energy: 400.0,  // GeV
    process: 'SoftQCD:inelastic',
    record: 'final_state',
  },

  fixed_target:: {
    cpp: 'fixed_target_source',
    beam_energy: 400.0,  // GeV
    target_z: 74,  // proton number, not a coordinate
    target_a: 184,
    // Target z extents to be determined from GeoModel geometry
    target_z_start: 0.0,  // mm
    target_z_end: 1164.0,  // mm
    interaction_length: 191.9,  // mm
    tau0_threshold: 1.0,  // mm/c
    record: 'final_state',
  },

  // Neutrino interactions pre-generated with GENIE, read from a rootracker
  // file (gntpc -f rootracker); see docs/genie.md for how to produce one.
  genie_reader:: {
    cpp: 'genie_reader_source',
    file: 'genie_events.rootracker.root',
    record: 'final_state',
  },

  // Beam-line particle files from the BDSIM simulation of the P42/BDF line
  // (one TTree, one particle per entry; see docs/beam_files.md). Each entry
  // becomes an event with one particle, its weight in the event header.
  // `z` (mm) places the file's plane in the aegir frame, whose origin is the
  // target front face; x and y are taken from the file as they are, plus the
  // optional `x_offset`/`y_offset` (mm). `tree` defaults to the file's only
  // TTree. `project_to_z` (mm) moves each particle in a straight line from
  // the plane to that z before tracking. Files on EOS can be read directly over xrootd:
  //   root://eospublic.cern.ch//eos/workspace/s/sba-sim/north-area/p42/ship-beamfiles/<file>
  beam_file:: {
    cpp: 'beam_file_source',
    file: error 'beam_file: set `file`',
    z: error 'beam_file: set `z` (mm)',
  },

  // The beam at the entrance of the BDF shielding, 7.81 m upstream of the
  // target front face (the *-beam-* files, tree DRIFT_96). Weight 1 per proton.
  // The geometry has air, not the vacuum beam pipe, upstream of the target,
  // where about 1% of the protons interact; merge in project_to_z (e.g.
  // -100.0) to start them just in front of the target instead.
  p42_beam:: self.beam_file { z: -7810.0 },

  // Beam-induced background behind the hadron stopper, 4.444 m downstream of
  // the target front face: in the air gap between the hadron absorber (ends
  // at 4350 mm) and the first muon-shield magnet (*_mibs-in_HADRON_STOPPER
  // files). Weights are per proton on target.
  p42_background:: self.beam_file { z: 4444.0 },

  // ── modules ────────────────────────────────────────────────────────────
  // Without a `seed`, each run draws a random one (logged at startup, so a
  // run can be reproduced after the fact). Merge one in for reproducible
  // output, e.g. lib.geant4 { seed: 12345 }.
  geant4:: {
    cpp: 'geant4_module',
    verbosity: 0,
  },

  // Crossing-mode Geant4 for the GeoModel workflows: scoring-plane SD with a
  // KE threshold, production-cut regions in the target/absorber, tracked-
  // particle KE cut. Seeding works as for `geant4` above.
  geant4_crossing:: {
    cpp: 'geant4_module',
    sd_mode: 'crossing',
    ke_threshold: 0.5,  // GeV
    energy_cut: true,
    particle_ke_cut: 1.0,  // GeV
    regions: { '/SHiP/target': 50, '/SHiP/muon_shield/magn_absorb': 50 },  // production cuts [mm]
  },

  // FairShip's exitHadronAbsorber scoring plane for the GeoModel geometry, as
  // an entry for the geant4 module's `scoring_planes`: 1 cm behind the
  // hadron absorber, in the air gap before the first muon-shield
  // magnet (absorber end at z = 4350 mm, magn_1 from 4550 mm), with
  // FairShip's size (3.56 m - 1 mm by 1.7 m - 1 mm half-widths, 1 mm
  // half-thickness). That is wider than the muon-shield container
  // (±1810 mm in x), which a scoring plane may be: it lives in its own
  // parallel world, so it also records what passes beside the shield.
  // It records every track reaching it — muons, neutrinos and all others —
  // and stops it, so nothing is tracked beyond. The tracks it records going
  // downstream are also written as plane_particles, as they entered the
  // plane and with their ancestry, for workflows/plane_replay.jsonnet to
  // simulate further. Merge in a `ke_threshold` (GeV) to match the run's energy cut.
  exit_hadron_absorber_plane:: {
    name: 'exit_hadron_absorber',
    id: 1,
    half_x: 3559.0,  // mm
    half_y: 1699.0,  // mm
    half_z: 1.0,  // mm
    after_volume: '/SHiP/muon_shield/magn_absorb',
    gap: 10.0,  // mm
    kill: true,
    record_particles: true,
  },

  noop_output:: { cpp: 'sim_output_module', mode: 'noop' },

  // The writing modes also accept two optional keys bounding the writer's
  // memory use (issue #77): cluster_size_mib (default 32) and fill_contexts
  // (default 4, or the phlex thread count if lower). Override by merging, e.g.
  //   lib.full_output(f, h) { cluster_size_mib: 16, fill_contexts: 8 }
  mc_only_output(rntuple_file, histo_file):: {
    cpp: 'sim_output_module',
    mode: 'mc_only',
    rntuple_file: rntuple_file,
    histo_file: histo_file,
  },

  full_output(rntuple_file, histo_file):: {
    cpp: 'sim_output_module',
    mode: 'full',
    rntuple_file: rntuple_file,
    histo_file: histo_file,
  },
}
