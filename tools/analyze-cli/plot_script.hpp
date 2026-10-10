#pragma once

// plot.py, written next to the CSVs by analyze-cli. Needs numpy and matplotlib.
namespace tools {

inline constexpr const char* kPlotScript = R"PY(#!/usr/bin/env python3
"""Plot analyze-cli output. Usage: python3 plot.py [--start S] [--end S] [--out FILE]"""
import argparse
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--start", type=float, default=0.0)
ap.add_argument("--end", type=float, default=None)
ap.add_argument("--out", default=os.path.join(here, "analysis.png"))
args = ap.parse_args()

with open(os.path.join(here, "signals.csv")) as f:
    rows = list(csv.reader(f))
names = rows[0]
data = np.array(rows[1:], dtype=float)
t = data[:, 0]
end = args.end if args.end is not None else t[-1]
sel = (t >= args.start) & (t <= end)

events = {}
with open(os.path.join(here, "events.csv")) as f:
    for row in csv.DictReader(f):
        events.setdefault(row["signal"], []).append((float(row["time"]), float(row["velocity"])))

bands = [n[len("band."):] for n in names if n.startswith("band.")]
fig, axes = plt.subplots(len(bands) + 1, 1, figsize=(16, 2.0 * (len(bands) + 1)), sharex=True)
beats = [b for b, _ in events.get("beat", []) if args.start <= b <= end]
downbeats = [b for b, _ in events.get("downbeat", []) if args.start <= b <= end]

for ax, band in zip(axes, bands):
    raw = data[:, names.index("band." + band)]
    env = data[:, names.index("env." + band)]
    ax.fill_between(t[sel], raw[sel], color="#9bb7d4", label="level")
    ax.plot(t[sel], env[sel], color="#1f4e79", lw=1.0, label="envelope")
    on = [(x, v) for x, v in events.get("onset." + band, []) if args.start <= x <= end]
    if on:
        xs, vs = zip(*on)
        ax.vlines(xs, 0, vs, color="#c0392b", lw=1.0, label="onsets")
    for b in downbeats:
        ax.axvline(b, color="#888", lw=0.6, ls="--")
    ax.set_ylim(0, 1.05)
    ax.set_ylabel(band)
kicks = [x for x, _ in events.get("kick", []) if args.start <= x <= end]
axes[0].plot(kicks, [1.0] * len(kicks), "v", color="#e67e22", ms=5, label="kicks")
axes[0].legend(loc="upper right", fontsize=8)
snares = [x for x, _ in events.get("snare", []) if args.start <= x <= end]
mid_row = axes[bands.index("Mid")] if "Mid" in bands else axes[0]
mid_row.plot(snares, [1.0] * len(snares), "v", color="#16a085", ms=5, label="snares")
mid_row.legend(loc="upper right", fontsize=8)

ax = axes[-1]
ax.plot(t[sel], data[sel, names.index("rms")], color="#2c3e50", lw=1.0, label="rms")
ax.plot(t[sel], data[sel, names.index("centroid")], color="#8e44ad", lw=1.0, label="centroid")
ax.vlines(beats, 0, 0.5, color="#27ae60", lw=0.8, label="beats")
ax.vlines(downbeats, 0, 1.0, color="#e67e22", lw=1.4, label="downbeats")
ax.set_ylim(0, 1.05)
ax.set_ylabel("rms / beats")
ax.set_xlabel("seconds")
ax.legend(loc="upper right", fontsize=8)

fig.tight_layout()
fig.savefig(args.out, dpi=110)
print("wrote", args.out)
)PY";

} // namespace tools
