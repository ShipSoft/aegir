// Maps converted with fairship_to_fieldmap are in the magnet's own frame;
// `translation` [x, y, z] (mm) places each one in the geometry. The values are
// where FairShip puts the same maps (python/geomGeant4.py), and they agree
// with the volume positions in the geometry repo:
//   - muon shield: map origin at the shield entrance, z = 2040 mm
//     (/SHiP/muon_shield spans z = 2039.3–31487.3 mm, 0.7 mm margin per end).
//     Convert it with `--symmetry quadrant_dipole` (FairShip's quadSymm).
//   - spectrometer: map origin at the magnet centre, z = 89570 mm.
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(100),
  sources: {
    geometry: lib.geomodel_geometry,
    field: lib.field_map([
      {
        name: 'MuonShield',
        volume_pattern: '/SHiP/muon_shield',
        file: 'muon_shield.root',
        map: 'muon_shield',
        translation: [0, 0, 2040],
      },
      {
        name: 'Spectrometer',
        volume_pattern: '/SHiP/magnet',
        file: 'spectrometer_dipole.root',
        map: 'spectrometer_dipole',
        translation: [0, 0, 89570],
      },
    ]),
    gun: lib.gun,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('gun_st_field_output.root', 'gun_st_field_validation.root'),
  },
}
