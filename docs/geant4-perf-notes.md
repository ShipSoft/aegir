<!--
SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration

SPDX-License-Identifier: LGPL-3.0-or-later
-->

# Analysis notes (Geant4 integration performance work)

Machine: i5-1334U (2 P-cores + 8 E-cores, 12 threads). Workload:
`gun_mt_bench`, 2000 events, matched g4 concurrency / phlex `-j`.
Protocol: `scripts/bench_baseline.sh`, results keyed by git SHA.

## Timeline

| SHA | Change | g4/j=4 | g4/j=8 | g4/j=12 |
|:---|:---|---:|---:|---:|
| 0f68a24 | pre-existing state | 5.58 s | 4.72 s | 4.19 s |
| a0346db | jemalloc shim | 5.00 s | 4.25 s (median) | 3.57 s |
| 6f0a778 | hit-buffer reserve + unique_ptr | neutral | neutral | neutral |

- The a0346db → 6f0a778 back-to-back comparison initially looked like a
  regression at j=12, but the raw Geant4-only span (which the change
  cannot affect) drifted up equally — thermal drift on this laptop. An
  interleaved A/B (8 pairs, alternating old/new plugin at j=12) shows
  the change neutral within noise (mean paired delta −49 ± 230 ms).
  Kept: it fixes a G4Event leak on exception and removes per-event
  reallocation churn that matters without jemalloc.
- One 105 s outlier appeared in one of ten a0346db j=8 runs; 30
  follow-up runs of the same configuration reproduced nothing (all
  4–5 s). Unexplained; keep watching subsequent benchmark runs.

## Where the per-event "framework overhead" actually is

`mean(simulate span) − mean(ProcessOneEvent span)` grows with thread
count (1.9 → 6.0 ms/event from j=4 to j=12), which looks like scheduling
overhead under saturation — but it is a startup artifact. The first
`simulate` call runs `init_master` (~1–2 s) inside `std::call_once`,
and every other worker's first call blocks on that same `call_once`
for the duration: N threads × ~1 s / 2000 events ≈ the whole effect
(measured 5.99 ms/event at 12 threads; top simulate spans are ~1 s,
exactly one per worker thread).

A steady-state estimate from the reported per-span medians
(`p50_simulate − p50_process`, unaffected by the one-per-worker startup
outliers) is 0.1–0.2 ms/event.
Aegir-side sub-spans are microseconds (build_primaries ~1 µs, flush
~1–2 µs). The integration hot path is already tight.

Consequence: eager G4 master initialisation (before the event loop)
would clean up the startup transient, but geometry/field arrive as
per-event data products, so the module cannot initialise at
registration time — a phlex design question (one-time/run-level
resources vs per-event products), not an aegir bug.

## Reproducibility and worker-kernel churn

Measured with `gun_st_full` (200 events, fixed gun seed):

- `phlex -j 1`: **reproducible** — the validation histograms
  `h_mc_multiplicity`, `h_mc_momentum`, and `h_mc_pdg` are bin-by-bin
  identical across runs (checked by `scripts/compare_histograms.py`).
- `concurrency: 1` with `-j 12`: **not reproducible.** Even though only
  one `simulate` call runs at a time, TBB hands successive calls to
  different pool threads; each new thread lazily builds its own
  `G4WorkerRunManagerKernel` with its own RNG stream, so the event
  sequence samples different streams run to run. This directly confirms
  the worker-kernel churn concern: the kernel count is bounded by the
  number of distinct TBB threads that ever touch `simulate`, not by the
  module's `concurrency`.
- Consequence for validation: physics comparisons must either run
  `-j 1` (exact) or compare distributions statistically. The
  single-threaded benchmark recipes in the justfile now pin `-j 1`.

## Voluntary context switches

~2 per event (4.1–4.3 k per 2000-event run), independent of thread
count and allocator. Matches the phlex `resumable_driver` design: each
event index is handed from a dedicated driver thread to the TBB input
node through two binary semaphores (two futex wake/wait pairs per
event). Target of the phlex fiber-driver prototype.
