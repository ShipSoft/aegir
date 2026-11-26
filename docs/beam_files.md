# Beam and beam-induced background from the beam-line simulation

The beam-line team simulates the P42 line to the BDF target with BDSIM and
provides two kinds of particle files:

- the beam at the entrance of the BDF shielding, 7.81 m upstream of the target
  front face, with a realistic beam profile;
- the background the beam line induces (muons and neutrinos from beam losses,
  beam-gas interactions and so on), recorded behind the hadron stopper,
  4.444 m downstream of the target front face.

aegir reads both with the `beam_file_source` plugin and tracks the particles
through the SHiP geometry with Geant4.

## The files

The files are on EOS, in
`/eos/workspace/s/sba-sim/north-area/p42/ship-beamfiles`. The same files are
attached to EDMS documents [3374114](https://edms.cern.ch/document/3374114/3.0)
(beam) and [3559707](https://edms.cern.ch/document/3559707/0.1) (background).
aegir reads them directly over xrootd, so there is no need to copy them:

```
root://eospublic.cern.ch//eos/workspace/s/sba-sim/north-area/p42/ship-beamfiles/<file>
```

Each file holds one TTree with one entry per particle crossing the plane:

| Branch       | Content                                         |
|--------------|-------------------------------------------------|
| `x`, `y`     | position in the plane [m]                       |
| `xp, yp, zp` | direction cosines                               |
| `p`          | momentum [GeV/c]                                |
| `t`          | time [ns], within the spill (up to about 1 s)   |
| `pdgID`      | PDG code                                        |
| `weight`     | statistical weight                              |

The beam files (`*-p42-postls3-ship-v23-*-beam-*.root`, tree `DRIFT_96`)
hold about 10^8 protons each, all with weight 1. The name says which of the
two beam distributions was used (`sps` or `gauss`), whether the collimators are in (`full-collimation`) or out
(`no-collimation`), and whether the beam is swept across the target
(`with-dilution`) or not.

The background files (`*_mibs-in_HADRON_STOPPER.root`, tree
`HADRON_STOPPER`) each hold one source of background: aperture losses,
beam-gas interactions, inelastic interactions in the foils of the BSI intensity
monitor, collimation leakage
and the dilution monitor. Their weights are per proton on target, so the sum
of the weights in a file is the number of particles per proton from that
source, and the sources add up. Each file also states what it contains: a
`description` and a `normalisation` note, and the number of protons it stands
for (`originalProtons`). aegir prints the description and the proton count
when it opens a file.

## How the plugin reads them

The entries have no event structure, and aegir's particle record has no
per-particle weight. So each entry becomes one event with one particle, and
the entry's weight goes into the event header (`event_header.weight`). The
entry number is stored as `event_header.original_event_id`.

The file does not record where its plane is. The `z` key places it in the
aegir frame, whose origin is the target front face, in mm. `x` and `y` are
converted to mm and used as they are, plus the optional `x_offset` and
`y_offset`. The momentum is `p` times the direction cosines.

`workflows/lib.libsonnet` has the two planes ready:

- `lib.p42_beam` sets `z: -7810.0`;
- `lib.p42_background` sets `z: 4444.0`, in the air gap between the hadron
  absorber (which ends at 4350 mm) and the first muon-shield magnet.

The other settings are:

| Key            | Default         | Meaning                                                  |
|----------------|-----------------|----------------------------------------------------------|
| `file`         | (required)      | local path or `root://` URL                              |
| `tree`         | the only TTree  | name of the tree, if the file has more than one          |
| `first_entry`  | 0               | first entry to read                                      |
| `project_to_z` | unset           | move each particle in a straight line to this z (mm)     |
| `keep_time`    | false           | keep the file's time; otherwise every particle has t = 0 |
| `weight_scale` | 1               | factor applied to every weight                           |

Asking for more events than the file has entries stops the job with an error.

### Starting the beam in front of the target

Between the beam plane and the target the real beam travels in a vacuum pipe,
but the aegir geometry has air there. About 1% of the protons interact in that
air. To avoid this, set `project_to_z` to a point just upstream of the target
(for example `-100.0`). Each proton is then moved along its direction to that
z and tracked from there. With `keep_time`, the flight time is added.

## Running

`workflows/beam_file_st.jsonnet` runs a file through the GeoModel geometry.
Count the entries first if you want to read the whole file:

```sh
f=root://eospublic.cern.ch//eos/workspace/s/sba-sim/north-area/p42/ship-beamfiles/2026-11-24_p42-postls3-v23_collimation-leakage_mibs-in_HADRON_STOPPER.root
pixi run python3 scripts/count_entries.py "$f"
pixi run phlex -c <(jsonnet --tla-str infile="$f" --tla-code events=1000 \
    --tla-str kind=background workflows/beam_file_st.jsonnet)
```

For the beam, use `--tla-str kind=beam` and usually `--tla-code project_to_z=-100`.
Reading over xrootd needs a valid Kerberos ticket (`kinit`).

To get rates, sum `event_header.weight` over the events you are interested in,
then divide by the fraction of the file you simulated. That gives the rate per
proton on target.

## Open questions

These are for the beam-line team and do not stop the plugin from being used:

- The files give x and y in the beam-line frame. aegir assumes that frame's
  axes are its own (x horizontal, y up, z along the beam, with the beam axis at
  x = y = 0 on the target). The sign of x and any offset or tilt at the
  target still need to be confirmed.
- Whether the spill time matters for the studies we run. If it does, use
  `keep_time`.
