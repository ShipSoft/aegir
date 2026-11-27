# Beam spot on the target

Event generators such as `fixed_target` and `pythia8` put every interaction
on the beam axis, at x = y = 0. The real beam hits the target over a spot a
few centimetres across, and with dilution it is swept over the target in a
circle during the spill. The `beam_spot_module` transform moves each
generated event to where a proton of the real beam would hit the target, so
any generator gets a realistic beam spot without its own copy of the model.

## How it works

The transform reads a collection of `MCParticle`s and writes a moved copy.
For each event it draws a beam proton:

1. a time in the spill, uniform between 0 and `spill_length` (1 s);
2. the beam centre at that time. This is a linear drift plus, with dilution,
   a circular sweep:

   ```
   x_c = x + x_drift t + radius_x cos(2 pi frequency t + phase)
   y_c = y + y_drift t + radius_y sin(2 pi frequency t + phase)
   ```

3. the offset about the centre, in each plane a uniform value between
   `-half_width` and `+half_width` plus a Gaussian with `sigma`. With
   `half_width` 0 the spot is Gaussian; otherwise the profile has a flat top
   with Gaussian edges;
4. the direction. At the target the beam diverges as if from a point
   upstream, so the slope is the offset over that distance:
   x' = x / `source_distance.x`. The sweep comes from its own distance
   (`sweep.source_distance_x`), as the dilution kickers sit elsewhere.

Every vertex of the event moves by the offset, adjusted for the slope over
its distance from `reference_z` (0, the target front face). Every momentum
turns to the proton's direction. With `keep_time` the spill time is added to
every particle's time.

The draws come from their own random stream, keyed on the event number. With
a fixed `seed` they are reproducible and do not depend on the generator or
the thread order.

| Key | Unit | Meaning |
|---|---|---|
| `particles_from` | | creator of the input collection (default `mc_particles`) |
| `centre.x`, `centre.y` | mm | centre at the start of the spill |
| `centre.x_drift`, `centre.y_drift` | mm/s | drift of the centre over the spill |
| `sweep.radius_x`, `sweep.radius_y` | mm | sweep radius (leave out for no dilution) |
| `sweep.frequency` | Hz | turns per second; negative turns clockwise with x to the right and y up |
| `sweep.phase` | rad | angle of the centre at t = 0 |
| `sweep.source_distance_x`, `_y` | m | distance for the sweep's slope (default: `source_distance`) |
| `spot.sigma_x`, `spot.sigma_y` | mm | Gaussian width about the centre |
| `spot.half_width_x`, `spot.half_width_y` | mm | flat-top half-width (default 0) |
| `source_distance.x`, `.y` | m | distance for the slope (leave out for a parallel beam) |
| `spill_length` | s | default 1 |
| `reference_z` | mm | z at which the offset applies (default 0) |
| `keep_time` | | add the spill time to the particle times (default false) |
| `seed` | | random seed (default: drawn and logged) |

## Using it

Add the transform as a module and point Geant4 and the output at its
particles with `particles_from`, the name of the transform's block:

```jsonnet
modules: {
  beam_spot: lib.p42_beam_spot_sps,
  geant4: lib.geant4_crossing { particles_from: 'beam_spot' },
  output: lib.full_output('out.root', 'valid.root') { particles_from: 'beam_spot' },
},
```

`workflows/fixed_target_beam_spot.jsonnet` does this for `fixed_target`, with
the arguments `beam` (`sps` or `gauss`) and `dilution`. The output file holds
the moved particles under `mc_particles`.

## The P42 presets

`lib.libsonnet` has the beam of the P42 beam-line simulation (v23, full
collimation; see [beam_files.md](beam_files.md)), fitted with
`scripts/analyse_beam_spot.py` to 20 million protons from each file:

| Preset | Spot about the centre [mm] | Centre y [mm] | Sweep |
|---|---|---|---|
| `p42_beam_spot_sps` | flat top ±21.8 with σ 5.8 edges in x, σ 18.0 in y | −6.8 + 13.2 t | 48 mm, 4 Hz |
| `p42_beam_spot_sps_no_dilution` | flat top ±21.9 with σ 3.5 edges in x, σ 17.2 in y | −6.2 + 13.2 t | none |
| `p42_beam_spot_gauss` | σ 16.5 in x, σ 17.6 in y | −4.8 + 8.6 t | 48 mm, 4 Hz |
| `p42_beam_spot_gauss_no_dilution` | σ 15.9 in x, σ 16.7 in y | −4.3 + 8.6 t | none |

The x centre stays within 0.3 mm of the axis. The sweep turns four times per
spill, starting at the bottom of the circle, clockwise when drawn with x to
the right and y up. In the right-handed aegir frame that is the view looking
upstream, from the detector towards the target. Whether the beam-line files
use the same sign of x is one of the open questions in
[beam_files.md](beam_files.md). The y centre drifts up during the spill with and without dilution,
so the drift belongs to the extracted beam, not to the sweep. The spot seems
to come from about 113 m upstream in x and 142–146 m in y, and the sweep from
123 m in x and 115 m in y.

Run through the transform, the presets give the same distribution on the
target as the beam files to within a few per cent:

- the RMS of x and y, and the mean, spread and 99% quantile of the radius
  agree to within 1–4%;
- the RMS of the slopes agrees to within 2%;
- the y centre in the first and the second half of the spill agrees to
  within about 1 mm.

To refit, for another version of the beam files for example:

```sh
pixi run python3 scripts/analyse_beam_spot.py --entries 20000000 --outdir beam_spot <beam files>
```

This writes a JSON summary and a page of plots per file.

## Limits

- The transform moves events that the generator has already made to
  interact. It cannot model protons that miss the target. In the beam files,
  at most 10⁻⁴ of the protons with collimation, and up to 6 × 10⁻⁴ without,
  arrive more than 125 mm from the axis. To study those, simulate the beam
  files with `beam_file_source` instead.
- The flat-top profile describes the SPS beam's x distribution only
  approximately. It reproduces the RMS, but the top is neither quite flat
  nor quite symmetric.
- The momentum spread of the beam (about ±0.4 GeV/c around 400 GeV/c) is not
  applied. The generator's beam energy stays as configured.
