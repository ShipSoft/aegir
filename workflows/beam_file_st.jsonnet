// Simulate the particles of a beam-line file (the P42 beam or the
// beam-induced background from the BDSIM simulation of the line) through the
// SHiP geometry with Geant4. Each entry of the file becomes one event with
// one particle, its weight in the event header. See docs/beam_files.md.
//
// Top-level arguments (--tla-code / --tla-str):
//   infile       the beam-line file, a local path or a root:// URL (--tla-str)
//   events       number of entries to simulate, at most the file's entries:
//                python3 scripts/count_entries.py <infile>
//   kind         'beam' (plane at the BDF shielding entrance, z = -7810 mm)
//                or 'background' (plane behind the hadron stopper,
//                z = 4444 mm); default 'background'
//   first_entry  first entry to simulate (default 0)
//   project_to_z move each particle in a straight line from the file's plane
//                to this z (mm) before tracking, e.g. -100 for the beam:
//                upstream of the target the geometry has air where the beam
//                line is in vacuum (default: start at the plane)
//   seed         seeds Geant4, for reproducible batch jobs; without it each
//                run draws its own (logged at startup)
//   ecut         energy cut in GeV (default 0.5)
//
// Needs the GeoModel geometry DB (pixi run build_geometry, see README).
local lib = import 'lib.libsonnet';
function(infile, events, kind='background', first_entry=0, project_to_z=null,
         seed=null, ecut=0.5) {
  local presets = { beam: lib.p42_beam, background: lib.p42_background },
  assert std.objectHas(presets, kind) : "kind must be 'beam' or 'background'",
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    field: lib.null_field,
    geometry: lib.geomodel_geometry,
    input: presets[kind] {
      file: infile,
      first_entry: first_entry,
      [if project_to_z != null then 'project_to_z']: project_to_z,
    },
  },
  modules: {
    geant4: lib.geant4_crossing {
      ke_threshold: ecut,
      [if seed != null then 'seed']: seed,
    },
    output: lib.full_output('beam_file.root', 'beam_file_validation.root'),
  },
}
