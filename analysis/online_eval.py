#!/usr/bin/env python3
"""Prequential (test-then-train) evaluation of the online predictor, which is how it is deployed.

Samples are processed in time order. At every sample the model uses ONLY counts from earlier samples
(with exponential forgetting, as in PredictorService), predicts P(deadlock within k), and the prediction
is scored when its label becomes known. Compares forgetting factors and cold vs warm start, on the
light-load runs (the regime where the offline model trained on heavy load was badly calibrated).
Usage: python3 analysis/online_eval.py data/telemetry data/heldout [k=5]"""
import sys, os, json, numpy as np
K = int(sys.argv[3]) if len(sys.argv) > 3 else 5
TRAIN, TEST = sys.argv[1], sys.argv[2]
sys.argv = [sys.argv[0], TRAIN, str(K)]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_predictor as v

S, D, SM = 4, 3, 0.5

def risk_from_counts(C):
    return v.curve(v.estimate(C, SM), K)

def run(eps_runs, decay, C0=None, min_trans=200):
    """eps_runs: list of episode lists in time order. Returns (y, p, base)"""
    C = np.zeros((S, S)) if C0 is None else C0.copy()
    Y, P = [], []
    cache = {}
    for eps in eps_runs:
        for e in eps:
            lab = dict(v.labels(e, K))
            for i in range(len(e)):
                if i > 0:
                    if decay < 1: C *= decay
                    C[e[i - 1], e[i]] += 1
                if i in lab:
                    key = None
                    if C[:3].sum() < min_trans:
                        pr = None
                    else:
                        r = risk_from_counts(C)
                        pr = r[e[i]]
                    if pr is not None:
                        Y.append(lab[i]); P.append(pr)
    return np.array(Y), np.array(P)

def report(name, Y, P):
    if len(Y) == 0: return {"name": name, "n": 0}
    base = float(Y.mean())
    return {"name": name, "n": int(len(Y)), "base_rate": base, "brier": v.brier(Y, P),
            "brier_base_rate_const": v.brier(Y, np.full(len(Y), base)), "auc": v.auc(Y, P)}

def load_eps(prefix):
    runs = v.load(prefix + "_samples.csv")
    return [v.episodes(runs[r]) for r in sorted(runs)]

train = load_eps(TRAIN); test = load_eps(TEST)
Cwarm = v.fit_counts([e for eps in train for e in eps])
out = []
for decay in (1.0, 0.9999, 0.999, 0.99):
    Y, P = run(test, decay)
    out.append(report(f"cold start, forgetting {decay}", Y, P))
for decay in (1.0, 0.9999, 0.999):
    Y, P = run(test, decay, C0=Cwarm)
    out.append(report(f"warm start from heavy-load model, forgetting {decay}", Y, P))
# reference: the static heavy-load model
Y, P = [], []
risk = risk_from_counts(Cwarm)
for eps in test:
    for e in eps:
        for i, y in v.labels(e, K): Y.append(y); P.append(risk[e[i]])
out.append(report("static heavy-load model (no adaptation)", np.array(Y), np.array(P)))
json.dump(out, open("analysis/online_eval.json", "w"), indent=2)
print(f"Prequential evaluation on {TEST} (k={K} samples)")
for r in out:
    if r["n"]: print(f"{r['name']:<58} n={r['n']:6d} Brier={r['brier']:.4f} (const base-rate {r['brier_base_rate_const']:.4f}) AUC={r['auc']:.3f}")
