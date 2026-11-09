#!/usr/bin/env python3
"""Turn results/exp_*.json (benchmark_suite output) into LaTeX fragments for the paper.
Writes paper/generated/*.tex. Usage: python3 analysis/make_paper_tables.py results paper/generated"""
import json, os, sys, glob

RES = sys.argv[1] if len(sys.argv) > 1 else "results"
OUT = sys.argv[2] if len(sys.argv) > 2 else "paper/generated"
os.makedirs(OUT, exist_ok=True)
rows = []
for f in sorted(glob.glob(os.path.join(RES, "exp_*.json"))):
    rows += json.load(open(f))

ORDER = ["DETECT_YOUNGEST", "DETECT_VALUE", "BANKERS", "HYBRID", "ORDERED", "NATIVE_MYSQL"]
SHORT = {"DETECT_YOUNGEST": "Detect-Y", "DETECT_VALUE": "Detect-V", "BANKERS": "Banker's",
         "HYBRID": "Hybrid", "ORDERED": "Ordered", "NATIVE_MYSQL": "InnoDB"}
COLOR = {"DETECT_YOUNGEST": "blue", "DETECT_VALUE": "cyan!70!black", "BANKERS": "green!45!black",
         "HYBRID": "orange!90!black", "ORDERED": "purple", "NATIVE_MYSQL": "red!70!black"}
MARK = {"DETECT_YOUNGEST": "*", "DETECT_VALUE": "square*", "BANKERS": "triangle*",
        "HYBRID": "diamond*", "ORDERED": "pentagon*", "NATIVE_MYSQL": "x"}

def sel(exp, strat, **kw):
    for r in rows:
        if r["experiment"] == exp and r["strategy"] == strat and all(abs(r[k] - v) < 1e-9 for k, v in kw.items()):
            return r
    return None

def fmt_ms(r):  # median with range
    a = sorted(r["msAll"])
    return f"{r['elapsedMs']/1000:.2f} ({a[0]/1000:.2f}--{a[-1]/1000:.2f})"

# ---------- figure data: thread scaling and hold sweep
def plot(exp, xkey, xs, extra):
    out = []
    for s in ORDER:
        pts = []
        for x in xs:
            r = sel(exp, s, **{xkey: x}, **extra)
            if r: pts.append(f"({x},{r['elapsedMs']/1000:.3f})")
        if pts:
            out.append(f"\\addplot[{COLOR[s]},mark={MARK[s]},thick] coordinates {{{''.join(pts)}}};")
    out.append("\\legend{" + ",".join(SHORT[s] for s in ORDER) + "}")
    return "\n".join(out)

open(f"{OUT}/plot_threads.tex", "w").write(plot("thread_scaling", "threads", [2, 4, 8, 16], {"accounts": 5}))
open(f"{OUT}/plot_hold.tex", "w").write(plot("hold_time", "holdMs", [0, 5, 20, 50], {}).replace("(0,", "(0.5,"))

# ---------- main table: median (min--max) seconds, deadlocks, failed
def cell(r):
    return f"{fmt_ms(r)} & {r['deadlocks']:.0f} & {r['failed']:.0f}"

settings = [
    ("2 threads, 5 acc.", "thread_scaling", {"threads": 2, "accounts": 5}),
    ("8 threads, 5 acc.", "thread_scaling", {"threads": 8, "accounts": 5}),
    ("16 threads, 5 acc.", "thread_scaling", {"threads": 16, "accounts": 5}),
    ("8 threads, 100 acc.", "account_scaling", {"accounts": 100}),
    ("hot spot 0.5", "hot_spot", {"hotProb": 0.5}),
    ("hot spot 0.9", "hot_spot", {"hotProb": 0.9}),
]
L = []
for name, exp, kw in settings:
    cells = []
    for s in ORDER:
        r = sel(exp, s, **kw)
        cells.append("-" if r is None else f"{r['elapsedMs']/1000:.2f}")
    rng = []
    L.append(name + " & " + " & ".join(cells) + " \\\\")
open(f"{OUT}/table_main_rows.tex", "w").write("\n".join(L))

# ---------- detailed: spread, p95, deadlocks, failures for 16 threads and hot 0.9
D = []
for name, exp, kw in [("16 threads, 5 accounts", "thread_scaling", {"threads": 16, "accounts": 5}),
                      ("8 threads, 90\\% on 2 of 20 accounts", "hot_spot", {"hotProb": 0.9})]:
    D.append(f"\\multicolumn{{6}}{{@{{}}l}}{{\\textit{{{name}}}}}\\\\")
    for s in ORDER:
        r = sel(exp, s, **kw)
        if r:
            a = sorted(r["msAll"])
            D.append(f"{SHORT[s]} & {r['elapsedMs']/1000:.2f} & {a[0]/1000:.2f}--{a[-1]/1000:.2f} & {r['p95Ms']:.0f} & {r['deadlocks']:.0f} & {r['failed']:.0f} \\\\")
open(f"{OUT}/table_detail_rows.tex", "w").write("\n".join(D))

# ---------- hybrid thresholds
H = []
for s in ["DETECT_VALUE", "BANKERS"] + sorted({r["strategy"] for r in rows if r["strategy"].startswith("HYBRID_")}):
    r = sel("hybrid_thresholds", s)
    if r:
        a = sorted(r["msAll"])
        label = SHORT.get(s) or ("Hybrid " + s.replace("HYBRID_", "").replace("_", "/"))
        H.append(f"{label} & {r['elapsedMs']/1000:.2f} & {a[0]/1000:.2f}--{a[-1]/1000:.2f} & {r['deadlocks']:.0f} & {r['failed']:.0f} \\\\")
open(f"{OUT}/table_hybrid_rows.tex", "w").write("\n".join(H))

# ---------- sanity: money conserved everywhere, number of runs
print("rows:", len(rows), "all conserved:", all(r["moneyConserved"] for r in rows),
      "runs:", sum(len(r["msAll"]) for r in rows))
