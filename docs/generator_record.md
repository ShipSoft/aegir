# Generator record modes

The event generators accept a `record` option choosing how much of their event
record reaches the `mc_particles` product:

| Value | What is emitted |
|-------|-----------------|
| `final_state` (default) | Only the particles the generator left undecayed |
| `full` | The whole record — beams, intermediate states, decayed mothers and final-state particles alike |

It applies to `pythia8_source`, `fixed_target_source` and
`genie_reader_source`. An unrecognised value is an error, not a silent
fallback: a typo here would quietly change what ends up in the output.

```jsonnet
sources: {
  fixed_target: lib.fixed_target {
    beam_energy: 400.0,
    record: 'full',   // default: 'final_state'
  },
},
```

## Why you might want the full record

`final_state` discards the provenance. Mothers of final-state particles are
generally not themselves final state, so their `motherId` links dangle and are
set to `-1` — in practice almost every particle looks like a primary. With
`full`, the chain survives, and questions like "did this kaon come from charm
or from the beam remnant" become answerable from the output alone.

## What it costs

Less than the entry counts suggest. A 400 GeV `SoftQCD:inelastic` record holds
roughly 2.5x as many entries as its final-state subset (about 100 against 40 in
the sample the `generator_status` test uses), but the extra rows are
bookkeeping — small, correlated integers that RNTuple compresses well. Measured
over 50 events, `mc_only` output grew from 65 kB to 132 kB, a factor of 2.

In a full simulation the difference is negligible, because `sim_hits`
dominates: the same 5-event `full_output` file grew by 0.03%. Generation time
is unaffected — it is dominated by Geant4 tracking, not by writing extra rows.

The **simulation output is unaffected**. Geant4 tracks only final-state (HepMC
status 1) entries either way, and their relative order in the collection is the
same in both modes, so `sim_hits` and `sim_particles` come out bit-identical at
a given seed (verified: every validation histogram matches bin for bin) — see
[geant4_integration.md](geant4_integration.md#primary-selection). The
validation histograms are also filled from final-state entries only, so they
mean the same thing in both modes.

## Status codes

The emitted `status` is the HepMC status code. Pythia8 provides it directly
via `statusHepMC()`. GENIE's rootracker record uses its own `GHepStatus` set,
which `genie_reader_source` translates:

| GENIE `GHepStatus` | | HepMC |
|---|---|---|
| `kIStStableFinalState` (1) | | 1 (final state) |
| `kIStInitialState` (0) | incoming neutrino, target nucleus | 4 (beam) |
| `kIStDecayedState` (3) | | 2 (decayed) |
| `kIStIntermediateState` (2), `kIStCorrelatedNucleon` (10) … `kIStNucleonClusterTarget` (16) | | `100 + code`, inside the 11-200 generator-dependent band and reversible |
| `kIStUndefined` (-1) | | 0 (empty entry) |

## Mother links

`motherId` is always an index into the emitted collection, never into the
generator's own record, and `-1` means "no mother". Under `final_state` the
remap usually yields `-1`; under `full` it is the record index minus one
(Pythia's entry 0, the "system" pseudo-particle, is never emitted), with `-1`
for the beam particles.

An entry can have more than one mother, so `mothers` carries the complete
list and `motherId` is its first element (`-1` when the list is empty).

For Pythia the list comes straight from `Particle::motherList()`. That
accessor derives from `mother1`, `mother2` **and** the native status, so it
already resolves the six documented `mother1`/`mother2` combinations — a
carbon copy of the mother, a single mother, an inclusive *range* of
string-fragmentation mothers, two genuinely distinct mothers, and an
order-reversed variant — into one uniform list. Reading the raw pair instead
could not tell a range from a pair, because the discriminator is the native
status that `statusHepMC()` discards. It is empty for the beam particles,
whose history Pythia does not record.

For GENIE the list is built from `StdHepFm` and `StdHepLm`, treated as two
distinct parents rather than the endpoints of a range. `StdHepLm` is optional:
files written without it read as single-mother throughout.

Mothers that did not survive the filter are **dropped** from the list, not
recorded as `-1` — the data model reserves that sentinel for `motherId`. Under
`record: 'final_state'` that usually empties the list entirely, since the
mothers of final-state particles are generally not final state themselves.
Under `full` the whole chain survives; a typical 400 GeV record has multi-mother
entries with up to six parents apiece.

`SHiP::mothersAreConsistent()` checks the resulting invariants (every index in
range, no `-1` in the list, `mothers.front() == motherId`, elements distinct
and never self-referential); `SHiP::mothersArePopulated()` additionally
requires that an entry with a mother carries the full list. The
`generator_status` test asserts both on generated records.
