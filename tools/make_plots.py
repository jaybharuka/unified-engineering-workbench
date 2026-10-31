#!/usr/bin/env python3
"""Labelled PNG plots from the raw CSVs in results/. Usage: python tools/make_plots.py [results_dir]"""
import csv, sys, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

R = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "results")
def rd(n):
    with open(os.path.join(R, n), newline="") as f: return list(csv.DictReader(f))
def save(fig, n):
    fig.tight_layout(); fig.savefig(os.path.join(R, n), dpi=110); plt.close(fig)

# T(x): FE vs exact
fig, ax = plt.subplots(figsize=(6, 3.6))
ex = rd("thermal_solution_N80.csv")
ax.plot([float(r["x_m"]) for r in ex], [float(r["T_exact_C"]) for r in ex], "k-", lw=2, label="exact")
for n, m in ((10, "o"), (20, "s"), (40, "^")):
    d = rd(f"thermal_solution_N{n}.csv")
    ax.plot([float(r["x_m"]) for r in d], [float(r["T_fe_C"]) for r in d], m, ms=4, mfc="none", label=f"FE N={n}")
ax.set_xlabel("x (m)"); ax.set_ylabel("Temperature (°C)"); ax.set_title("1-D rod: FE nodal values vs exact (L=1 m)")
ax.legend(); ax.grid(alpha=.3); save(fig, "plot_temperature_profile.png")

# midpoint error vs x
fig, ax = plt.subplots(figsize=(6, 3.6))
for n in (10, 20, 40, 80):
    d = rd(f"thermal_midpoints_N{n}.csv")
    ax.plot([float(r["x_mid_m"]) for r in d], [float(r["abs_error_C"]) for r in d], ".-", label=f"N={n}")
ax.set_xlabel("element midpoint x (m)"); ax.set_ylabel("|T_FE,interp − T_exact| (°C)"); ax.set_yscale("log")
ax.set_title("Midpoint error (FE interpolation)"); ax.legend(); ax.grid(alpha=.3, which="both"); save(fig, "plot_midpoint_error.png")

# convergence
c = rd("thermal_convergence.csv")
h = [float(r["h_m"]) for r in c]; e = [float(r["max_midpoint_error_C"]) for r in c]
fig, ax = plt.subplots(figsize=(6, 3.6))
ax.loglog(h, e, "o-", label="max midpoint error")
ax.loglog(h, [125 * x * x for x in h], "k--", lw=1, label="q·h²/(8k) = 125 h²")
ax.set_xlabel("element size h (m)"); ax.set_ylabel("max midpoint error (°C)"); ax.set_title("Convergence: slope 2 (O(h²))")
ax.legend(); ax.grid(alpha=.3, which="both"); save(fig, "plot_convergence.png")

# speedup / efficiency
p = [r for r in rd("performance_summary.csv") if r["implementation"] == "pool"]
w = [int(r["workers"]) for r in p]
fig, ax = plt.subplots(figsize=(6, 3.6))
ax.plot(w, [float(r["speedup_T1_over_Tp"]) for r in p], "o-", label="speedup T1/Tp")
ax.plot(w, [float(r["efficiency"]) for r in p], "s-", label="efficiency")
ax.plot(w, w, "k--", lw=1, label="ideal speedup")
ax.set_xticks(w); ax.set_xlabel("workers"); ax.set_title("Batch solve: bounded pool (median of 5)")
ax.legend(); ax.grid(alpha=.3); save(fig, "plot_speedup.png")

# loss
b = rd("backprop_loss.csv")
fig, ax = plt.subplots(figsize=(6, 3.6))
ax.plot([int(r["step"]) for r in b], [float(r["loss"]) for r in b])
ax.set_yscale("log"); ax.set_xlabel("update step"); ax.set_ylabel("loss J = ½(ŷ−y)²")
ax.set_title("Backprop: loss vs step"); ax.grid(alpha=.3, which="both"); save(fig, "plot_backprop_loss.png")
print("plots written to", R)
