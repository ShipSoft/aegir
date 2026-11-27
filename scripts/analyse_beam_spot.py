#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Parametrise the beam on the target from the P42 beam-line files.

The beam files (see docs/beam_files.md) record the protons at the entrance of
the BDF shielding, 7.81 m upstream of the target front face. This script moves
them in straight lines to the target front face and describes what arrives
there:

- the spot: position and angle, mean and spread, as a Gaussian fit to the
  core and as RMS, with the fraction of protons in the tails;
- with dilution, the sweep that paints the beam over the target. The beam
  centre is fitted against spill time with
      x_c(t) = x_off + x_drift t + R_x cos(2 pi f t + phi_x)
  and likewise for y. The spot is then measured about the fitted centre,
  which also shows how well the model describes the beam;
- the momentum spread and the fraction of other particles.

It writes a JSON summary per file and, unless --no-plots, a set of PNGs.

    python3 scripts/analyse_beam_spot.py --entries 20000000 --outdir beam_spot \\
        root://eospublic.cern.ch//eos/.../2026-11-25-p42-postls3-ship-v23-sps-beam-full-collimation-with-dilution.root
"""

import argparse
import json
import math
import os
import sys

import ROOT

DISTANCE_M = 7.81  # beam plane to target front face
N_TIME_BINS = 1000  # 1 ms over the 1 s spill
WINDOW_MM = 200.0  # positions further out are not on the target at all
WINDOW_MRAD = 5.0

# PyROOT crashes when it frees RDataFrame nodes and results at the end of a
# function; keeping them here until os._exit avoids that.
_ALIVE = []


QUANTILES = (0.01, 0.1587, 0.5, 0.8413, 0.99)


def gauss_core(h):
    """Gaussian fit to the core (+-2 sigma, refitted twice), RMS and quantiles.

    The quantiles describe distributions that are not Gaussian (the momentum
    spread is closer to flat); sigma is null where the fit fails.
    """
    probs = ROOT.std.vector["double"](QUANTILES)
    qs = ROOT.std.vector["double"](len(QUANTILES))
    h.GetQuantiles(len(QUANTILES), qs.data(), probs.data())
    mean, width = h.GetMean(), h.GetStdDev()
    fitted = False
    for _ in range(3):
        r = h.Fit("gaus", "QS0", "", mean - 2 * width, mean + 2 * width)
        if not r.Get() or not r.IsValid() or r.Parameter(2) == 0:
            break
        mean, width, fitted = r.Parameter(1), abs(r.Parameter(2)), True
    result = {
        "rms": h.GetStdDev(),
        "mean_all": h.GetMean(),
        "quantiles": {f"{p:g}": q for p, q in zip(QUANTILES, qs)},
        "mean": mean if fitted else None,
        "sigma": width if fitted else None,
    }
    if fitted:
        lo, hi = h.FindBin(mean - 3 * width), h.FindBin(mean + 3 * width)
        total = h.Integral(0, h.GetNbinsX() + 1)
        result["fraction_outside_3sigma"] = (
            1.0 - h.Integral(lo, hi) / total if total else 0.0
        )
    return result


def flat_top(h):
    """Fit a uniform +-a convolved with a Gaussian sigma to a profile.

    a = 0 is a plain Gaussian; a > 0 gives a flat top with Gaussian edges, as
    in the SPS beam. This is the shape the beam_spot module samples (uniform
    plus Gaussian), so the fitted a and sigma go into its configuration.
    """
    rms = h.GetStdDev()
    # Normalised to the integral N, so that a -> 0 tends to a Gaussian
    # instead of blowing up the amplitude.
    f = ROOT.TF1(
        f"flat_top_{h.GetName()}",
        "[0]/(4*[2])*(TMath::Erf((x-[1]+[2])/(sqrt(2)*[3]))"
        " - TMath::Erf((x-[1]-[2])/(sqrt(2)*[3])))",
        h.GetXaxis().GetXmin(),
        h.GetXaxis().GetXmax(),
    )
    norm = h.Integral() * h.GetXaxis().GetBinWidth(1)
    best = None
    # From Gaussian-like and from box-like starting points; keep the better.
    for a0, s0 in ((0.05 * rms, rms), (1.5 * rms, 0.3 * rms)):
        f.SetParameters(norm, h.GetMean(), a0, s0)
        f.SetParLimits(2, 1e-3 * rms, 5 * rms)
        f.SetParLimits(3, 1e-2 * rms, 5 * rms)
        r = h.Fit(f, "QS0")
        if r.Get() and r.IsValid() and (best is None or r.Chi2() < best[0]):
            best = (
                r.Chi2(),
                r.Ndf(),
                f.GetParameter(1),
                f.GetParameter(2),
                f.GetParameter(3),
            )
    if best is None:
        return None
    chi2, ndf, mean, a, sigma = best
    return {
        "mean": mean,
        "half_width": a,
        "sigma": sigma,
        "chi2_ndf": chi2 / ndf if ndf else None,
    }


def source_distance_m(h2):
    """Distance [m] to the point the spot seems to come from: x / x'."""
    slope = h2.GetCovariance() / h2.GetRMS(1) ** 2  # mrad per mm
    return 1.0 / slope if slope else None


def unwrapped_turns(xs, ys):
    """Turns of the centre around the origin over the spill (sign: sense)."""
    angle, prev, offset = 0.0, None, 0.0
    first = None
    for x, y in zip(xs, ys):
        a = math.atan2(y, x)
        if prev is not None:
            while a + offset - prev > math.pi:
                offset -= 2 * math.pi
            while a + offset - prev < -math.pi:
                offset += 2 * math.pi
        angle = a + offset
        first = angle if first is None else first
        prev = angle
    return (angle - first) / (2 * math.pi)


def fit_sweep(profile, frequency, cosine):
    """Fit off + drift t + R cos/sin(2 pi f t + phi) to a profile in t [s]."""
    shape = "cos" if cosine else "sin"
    f = ROOT.TF1(
        f"sweep_{profile.GetName()}",
        f"[0] + [1]*x + [2]*{shape}(2*TMath::Pi()*[3]*x + [4])",
        0,
        1,
    )
    f.SetParameters(
        profile.GetMean(2), 0.0, math.sqrt(2) * profile.GetStdDev(2), frequency, 0.0
    )
    best = None
    for phase in (0.0, math.pi / 2, math.pi, -math.pi / 2):  # avoid local minima
        f.SetParameter(3, frequency)
        f.SetParameter(4, phase)
        r = profile.Fit(f, "QS0")
        if best is None or r.Chi2() < best[0]:
            best = (r.Chi2(), [f.GetParameter(i) for i in range(5)], r.Ndf())
    pars = best[1]
    if pars[2] < 0:  # keep the amplitude positive
        pars[2], pars[4] = -pars[2], pars[4] + math.pi
    pars[4] = math.remainder(pars[4], 2 * math.pi)
    return {
        "offset": pars[0],
        "drift_per_s": pars[1],
        "amplitude": pars[2],
        "frequency_hz": pars[3],
        "phase_rad": pars[4],
        "chi2_ndf": best[0] / best[2] if best[2] else float("nan"),
    }


def analyse(path, entries, outdir, plots):
    name = os.path.basename(path).removesuffix(".root")
    f = ROOT.TFile.Open(path)
    total = f.Get("DRIFT_96").GetEntries()
    f.Close()
    df = ROOT.RDataFrame("DRIFT_96", path)
    if entries:
        df = df.Range(entries)
    # Straight-line drift to the target front face; mm, mrad, s.
    df = (
        df.Define("x0", f"1000*(x + xp/zp*{DISTANCE_M})")
        .Define("y0", f"1000*(y + yp/zp*{DISTANCE_M})")
        .Define("xpr", "1000*xp/zp")
        .Define("ypr", "1000*yp/zp")
        .Define("ts", "t*1e-9")
    )
    protons = df.Filter("pdgID == 2212")
    beam = protons.Filter(
        f"p > 390 && abs(x0) < {WINDOW_MM} && abs(y0) < {WINDOW_MM}"
        f" && abs(xpr) < {WINDOW_MRAD} && abs(ypr) < {WINDOW_MRAD}"
    )

    tb = (N_TIME_BINS, 0.0, 1.0)
    booked = {
        "n": df.Count(),
        "n_protons": protons.Count(),
        "n_beam": beam.Count(),
        "n_low_p": protons.Filter("p <= 390").Count(),
        "p": protons.Histo1D(("p", "momentum;p [GeV/c]", 4000, 380, 420), "p"),
        "t": df.Histo1D(("t", "spill time;t [s]", 100, 0, 1), "ts"),
        "x0": beam.Histo1D(("x0", ";x [mm]", 800, -WINDOW_MM, WINDOW_MM), "x0"),
        "y0": beam.Histo1D(("y0", ";y [mm]", 800, -WINDOW_MM, WINDOW_MM), "y0"),
        "xpr": beam.Histo1D(
            ("xpr", ";x' [mrad]", 1000, -WINDOW_MRAD, WINDOW_MRAD), "xpr"
        ),
        "ypr": beam.Histo1D(
            ("ypr", ";y' [mrad]", 1000, -WINDOW_MRAD, WINDOW_MRAD), "ypr"
        ),
        "xy": beam.Histo2D(
            ("xy", "on target;x [mm];y [mm]", 200, -150, 150, 200, -150, 150),
            "x0",
            "y0",
        ),
        "cx": beam.Profile1D(("cx", ";t [s];<x> [mm]", *tb), "ts", "x0"),
        "cy": beam.Profile1D(("cy", ";t [s];<y> [mm]", *tb), "ts", "y0"),
        "cxp": beam.Profile1D(("cxp", ";t [s];<x'> [mrad]", *tb), "ts", "xpr"),
        "cyp": beam.Profile1D(("cyp", ";t [s];<y'> [mrad]", *tb), "ts", "ypr"),
        "r_all": protons.Define("r", "sqrt(x0*x0 + y0*y0)").Histo1D(
            ("r_all", "all protons;r [mm]", 1000, 0, 1000), "r"
        ),
    }
    ROOT.RDF.RunGraphs(list(booked.values()))
    g = {k: v.GetValue() for k, v in booked.items()}

    summary = {
        "file": path,
        "entries_in_file": total,
        "entries_read": g["n"],
        "distance_to_target_m": DISTANCE_M,
        "fraction_not_proton": 1.0 - g["n_protons"] / g["n"],
        "fraction_proton_p_below_390": g["n_low_p"] / g["n"],
        "fraction_outside_core_window": 1.0 - g["n_beam"] / g["n"],
        "momentum": gauss_core(g["p"]),
    }
    r_all = g["r_all"]
    summary["fraction_r_above"] = {
        f"{r}mm": r_all.Integral(r_all.FindBin(r), r_all.GetNbinsX() + 1)
        / g["n_protons"]
        for r in (100, 125, 150, 200)
    }

    cx, cy = g["cx"], g["cy"]
    xs = [cx.GetBinContent(b) for b in range(1, N_TIME_BINS + 1)]
    ys = [cy.GetBinContent(b) for b in range(1, N_TIME_BINS + 1)]
    turns = unwrapped_turns(xs, ys)
    # Swept if the centre moves by more than a few mm: with an unswept beam
    # the centre sits near the origin and its angle jitters, so the turn
    # count alone can be noise.
    motion = math.sqrt(
        sum((v - sum(xs) / len(xs)) ** 2 for v in xs) / len(xs)
        + sum((v - sum(ys) / len(ys)) ** 2 for v in ys) / len(ys)
    )
    swept = motion > 5.0 and abs(turns) > 0.5
    summary["swept"] = swept
    summary["centre_motion_rms_mm"] = motion

    if swept:
        f0 = turns  # per 1 s spill
        sweep = {
            "x": fit_sweep(cx, f0, cosine=True),
            "y": fit_sweep(cy, f0, cosine=False),
            "xp": fit_sweep(g["cxp"], f0, cosine=True),
            "yp": fit_sweep(g["cyp"], f0, cosine=False),
        }
        sweep["turns_per_spill"] = turns
        summary["sweep"] = sweep
        sx, sy = sweep["x"], sweep["y"]
        sxp, syp = sweep["xp"], sweep["yp"]
        centre = (
            "double cx(double t) {{ return {0} + {1}*t + {2}*cos(2*TMath::Pi()*{3}*t + {4}); }}\n"
            "double cy(double t) {{ return {5} + {6}*t + {7}*sin(2*TMath::Pi()*{8}*t + {9}); }}\n"
            "double cxp(double t) {{ return {10} + {11}*t + {12}*cos(2*TMath::Pi()*{13}*t + {14}); }}\n"
            "double cyp(double t) {{ return {15} + {16}*t + {17}*sin(2*TMath::Pi()*{18}*t + {19}); }}\n"
        )
        params = [
            s[k]
            for s in (sx, sy, sxp, syp)
            for k in ("offset", "drift_per_s", "amplitude", "frequency_hz", "phase_rad")
        ]
        ns = f"sweep_{abs(hash(path))}"
        ROOT.gInterpreter.Declare(f"namespace {ns} {{\n" + centre.format(*params) + "}")
        rel = (
            beam.Define("dx", f"x0 - {ns}::cx(ts)")
            .Define("dy", f"y0 - {ns}::cy(ts)")
            .Define("dxp", f"xpr - {ns}::cxp(ts)")
            .Define("dyp", f"ypr - {ns}::cyp(ts)")
        )
    else:
        # No sweep: the centre may still drift linearly over the spill.
        drift = {}
        for key, prof in (("x", cx), ("y", cy), ("xp", g["cxp"]), ("yp", g["cyp"])):
            r = prof.Fit("pol1", "QS0")
            drift[key] = {
                "offset": r.Parameter(0),
                "drift_per_s": r.Parameter(1),
                "chi2_ndf": r.Chi2() / r.Ndf() if r.Ndf() else float("nan"),
            }
        summary["centre"] = drift
        d = [
            drift[k][q]
            for k in ("x", "y", "xp", "yp")
            for q in ("offset", "drift_per_s")
        ]
        rel = (
            beam.Define("dx", f"x0 - ({d[0]} + {d[1]}*ts)")
            .Define("dy", f"y0 - ({d[2]} + {d[3]}*ts)")
            .Define("dxp", f"xpr - ({d[4]} + {d[5]}*ts)")
            .Define("dyp", f"ypr - ({d[6]} + {d[7]}*ts)")
        )

    spot = {
        "dx": rel.Histo1D(("dx", ";x - x_{c} [mm];", 800, -100, 100), "dx"),
        "dy": rel.Histo1D(("dy", ";y - y_{c} [mm];", 800, -100, 100), "dy"),
        "dxp": rel.Histo1D(("dxp", ";x' - x'_{c} [mrad];", 1000, -2.5, 2.5), "dxp"),
        "dyp": rel.Histo1D(("dyp", ";y' - y'_{c} [mrad];", 1000, -2.5, 2.5), "dyp"),
        "dxdy": rel.Histo2D(
            (
                "dxdy",
                "about the centre;x - x_{c} [mm];y - y_{c} [mm]",
                160,
                -80,
                80,
                160,
                -80,
                80,
            ),
            "dx",
            "dy",
        ),
        "xphase": rel.Histo2D(
            ("xphase", ";x - x_{c} [mm];x' - x'_{c} [mrad]", 160, -80, 80, 160, -1, 1),
            "dx",
            "dxp",
        ),
        "yphase": rel.Histo2D(
            ("yphase", ";y - y_{c} [mm];y' - y'_{c} [mrad]", 160, -80, 80, 160, -1, 1),
            "dy",
            "dyp",
        ),
    }
    spot = {k: v.GetValue() for k, v in spot.items()}
    summary["spot"] = {
        "x_mm": gauss_core(spot["dx"]),
        "y_mm": gauss_core(spot["dy"]),
        "xp_mrad": gauss_core(spot["dxp"]),
        "yp_mrad": gauss_core(spot["dyp"]),
        "corr_x_xp": spot["xphase"].GetCorrelationFactor(),
        "corr_y_yp": spot["yphase"].GetCorrelationFactor(),
        "corr_x_y": spot["dxdy"].GetCorrelationFactor(),
        "x_flat_top_mm": flat_top(spot["dx"]),
        "y_flat_top_mm": flat_top(spot["dy"]),
        "source_distance_x_m": source_distance_m(spot["xphase"]),
        "source_distance_y_m": source_distance_m(spot["yphase"]),
    }
    if swept:
        sw = summary["sweep"]
        sw["source_distance_x_m"] = sw["x"]["amplitude"] / sw["xp"]["amplitude"]
        sw["source_distance_y_m"] = sw["y"]["amplitude"] / sw["yp"]["amplitude"]

    _ALIVE.extend((df, protons, beam, rel, booked, g, spot))
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, f"{name}.json"), "w") as out:
        json.dump(summary, out, indent=2)
    if plots:
        draw(name, outdir, g, spot, summary)
    return summary


def draw(name, outdir, g, spot, summary):
    ROOT.gStyle.SetOptStat(0)
    c = ROOT.TCanvas(f"c_{name}", "", 1800, 1200)
    c.Divide(3, 2)
    c.cd(1)
    g["xy"].SetTitle(name[-60:])
    g["xy"].Draw("colz")
    c.cd(2)
    spot["dxdy"].Draw("colz")
    c.cd(3)
    frame = ROOT.TH1F(f"frame_{name}", "beam centre;t [s];centre [mm]", 1, 0, 1)
    frame.SetMinimum(-80)
    frame.SetMaximum(80)
    frame.Draw()
    g["cx"].SetLineColor(ROOT.kBlue)
    g["cy"].SetLineColor(ROOT.kRed)
    g["cx"].Draw("same hist")
    g["cy"].Draw("same hist")
    c.cd(4)
    spot["xphase"].Draw("colz")
    c.cd(5)
    spot["yphase"].Draw("colz")
    c.cd(6).SetLogy()
    g["r_all"].Draw()
    c.SaveAs(os.path.join(outdir, f"{name}.png"))
    _ALIVE.extend((c, frame))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("files", nargs="+", help="beam files, local paths or root:// URLs")
    ap.add_argument(
        "--entries", type=int, default=0, help="entries to read per file (0: all)"
    )
    ap.add_argument("--outdir", default="beam_spot")
    ap.add_argument("--no-plots", action="store_true")
    ap.add_argument(
        "--threads",
        type=int,
        default=0,
        help="threads when reading whole files (0: all cores)",
    )
    args = ap.parse_args()
    ROOT.gROOT.SetBatch(True)
    if not args.entries:  # Range, for --entries, needs a single thread
        ROOT.EnableImplicitMT(args.threads)
    for f in args.files:
        s = analyse(f, args.entries, args.outdir, not args.no_plots)
        sp = s["spot"]
        line = (
            f"{os.path.basename(f)}\n  spot sigma x {sp['x_mm']['sigma']:.2f} mm, y {sp['y_mm']['sigma']:.2f} mm;"
            f" x' {sp['xp_mrad']['sigma']:.3f} mrad, y' {sp['yp_mrad']['sigma']:.3f} mrad"
        )
        if s["swept"]:
            sw = s["sweep"]
            line += (
                f"\n  sweep {sw['x']['frequency_hz']:.3f} Hz, R_x {sw['x']['amplitude']:.1f} mm,"
                f" R_y {sw['y']['amplitude']:.1f} mm, turns {sw['turns_per_spill']:.2f}"
            )
        print(line)
        sys.stdout.flush()
    # Skip interpreter shutdown, where PyROOT tears down the RDataFrame
    # results in the wrong order and crashes.
    os._exit(0)


if __name__ == "__main__":
    main()
