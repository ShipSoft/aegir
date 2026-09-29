// Maps converted with fairship_to_fieldmap are in the magnet's own frame; add
// a `translation` [x, y, z] (mm) to each magnet to place it in the geometry.
local lib = import 'lib.libsonnet';
{
  driver: lib.driver(100),
  sources: {
    geometry: lib.geomodel_geometry,
    field: lib.field_map([
      {
        name: 'MuonShield',
        volume_pattern: 'MuonShield',
        file: 'muon_shield.root',
        map: 'muon_shield',
      },
      {
        name: 'Spectrometer',
        volume_pattern: 'SpectrometerDipole',
        file: 'spectrometer_dipole.root',
        map: 'spectrometer_dipole',
      },
    ]),
    gun: lib.gun,
  },
  modules: {
    geant4: lib.geant4,
    output: lib.full_output('gun_st_field_output.root', 'gun_st_field_validation.root'),
  },
}
