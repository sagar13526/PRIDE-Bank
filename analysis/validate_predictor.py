#!/usr/bin/env python3
"""Offline validation of the absorbing-Markov-chain deadlock predictor.

Input : <prefix>_samples.csv  (telemetry, one row per 10 ms sample, column `run`)
Output: analysis/validation_report.md and analysis/validation.json

Protocol
  * Samples become sequences per run; idle samples (no transfers in flight) end an episode;
    a transition out of DEADLOCK (state 3) is not counted (absorbing).
  * 3-fold cross-validation BY RUN: a model is fitted on two thirds of the runs and scored on
    runs it never saw (samples of one run are strongly correlated, so splitting by sample would leak).
  * Task: at every non-deadlock sample, predict P(a deadlock is resolved within the next k samples).
  * Compared: Markov chain | constant hazard | per-state frequency | second-order chain.
  * Metrics: Brier score, log loss, ROC AUC, calibration table.
Same estimator as src/predict/markov_predictor.cpp (counts, additive smoothing on transient
columns, unseen row = self loop).
"""
import csv, json, sys, subprocess, os
from collections import defaultdict
import numpy as np

PREFIX = sys.argv[1] if len(sys.argv) > 1 else "data/telemetry"
K = int(sys.argv[2]) if len(sys.argv) > 2 else 5           # horizon in samples (10 ms each)
HELDOUT = sys.argv[3] if len(sys.argv) > 3 else None       # optional fresh dataset prefix
SMOOTH = 0.5
S, D = 4, 3

HIGH_RATIO = 0.75   # must match ClassifierConfig::highWaitRatio in include/predict/telemetry.h

def classify(active, waiting, new_deadlocks):
    """Same rule as classifyState() in src/predict/telemetry.cpp (wait-chain criterion off)."""
    if new_deadlocks > 0: return 3
    if waiting <= 0: return 0
    return 2 if (waiting >= 2 and active > 0 and waiting >= HIGH_RATIO * active) else 1

def load(path):
    runs = defaultdict(list)
    with open(path) as f:
        for r in csv.DictReader(f):
            a, w, d = int(r["active"]), int(r["waiting"]), int(r["newDeadlocks"])
            runs[int(r["run"])].append((a, classify(a, w, d)))
    return runs

def episodes(samples):
    """Split one run into episodes: break at idle samples and after every deadlock."""
    eps, cur = [], []
    for active, st in samples:
        if active == 0 and st == 0:
            if cur: eps.append(cur); cur = []
            continue
        cur.append(st)
        if st == D:
            eps.append(cur); cur = []
    if cur: eps.append(cur)
    return eps

def fit_counts(eps_list):
    C = np.zeros((S, S))
    for ep in eps_list:
        for a, b in zip(ep[:-1], ep[1:]):
            C[a, b] += 1          # a is never D inside an episode (episodes end at D)
    return C

def estimate(C, smooth=SMOOTH):
    P = np.zeros((S, S))
    for i in range(3):
        n = C[i].sum()
        if n <= 0: P[i, i] = 1; continue
        row = C[i].copy(); row[:3] += smooth
        P[i] = row / (n + smooth * 3)
    P[3, 3] = 1
    return P

def curve(P, k):
    """risk[s] = (P^k)[s][3]"""
    return np.linalg.matrix_power(P, k)[:, D]

def absorbing_stats(P):
    reach = {3}
    ch = True
    while ch:
        ch = False
        for i in range(3):
            if i not in reach and any(P[i, j] > 0 and j in reach for j in range(4)):
                reach.add(i); ch = True
    fin = [i for i in range(3) if i in reach]
    ch = True
    while ch:
        ch = False
        for i in list(fin):
            if any(P[i, j] > 0 and j < 3 and j not in fin for j in range(3)):
                fin.remove(i); ch = True
    E, SD = [np.inf] * 3, [np.inf] * 3
    if fin:
        Q = P[np.ix_(fin, fin)]
        N = np.linalg.inv(np.eye(len(fin)) - Q)
        t = N.sum(1); var = (2 * N - np.eye(len(fin))) @ t - t * t
        for a, i in enumerate(fin): E[i] = t[a]; SD[i] = float(np.sqrt(max(var[a], 0)))
    return E, SD

def labels(ep, k, last_is_censored=True):
    """For each position i of an episode: did a deadlock occur within the next k samples?
    Points whose k-sample window runs past the end of an episode that did NOT end in a deadlock
    are censored (we cannot know what would have happened) and are left out, not scored as 0."""
    out = []
    n = len(ep)
    ends_dl = n > 0 and ep[-1] == D
    for i in range(n):
        if ep[i] == D: continue
        hit = any(ep[j] == D for j in range(i + 1, min(n, i + 1 + k)))
        if not hit and not ends_dl and i + k >= n: continue   # censored
        out.append((i, int(hit)))
    return out

def auc(y, p):
    y = np.asarray(y); p = np.asarray(p)
    pos, neg = p[y == 1], p[y == 0]
    if len(pos) == 0 or len(neg) == 0: return float("nan")
    order = np.argsort(np.concatenate([pos, neg]), kind="mergesort")
    ranks = np.empty(len(order)); allv = np.concatenate([pos, neg])
    # average ranks for ties
    sv = allv[order]; r = np.empty(len(sv)); i = 0
    while i < len(sv):
        j = i
        while j + 1 < len(sv) and sv[j + 1] == sv[i]: j += 1
        r[i:j + 1] = (i + j) / 2 + 1; i = j + 1
    ranks[order] = r
    return (ranks[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg))

def brier(y, p): y = np.asarray(y); p = np.asarray(p); return float(np.mean((p - y) ** 2))
def logloss(y, p):
    y = np.asarray(y); p = np.clip(np.asarray(p), 1e-6, 1 - 1e-6)
    return float(-np.mean(y * np.log(p) + (1 - y) * np.log(1 - p)))

class Models:
    """Everything fitted from a set of training episodes."""
    def __init__(self, tr_eps):
        self.C = fit_counts(tr_eps)
        self.P = estimate(self.C)
        self.risk = curve(self.P, K)
        trans = self.C[:3].sum(); dl = self.C[:3, D].sum()
        h = dl / trans if trans else 0.0
        self.hz = 1 - (1 - h) ** K
        cnt = np.zeros(3); pos = np.zeros(3)
        for e in tr_eps:
            for i, y in labels(e, K): cnt[e[i]] += 1; pos[e[i]] += y
        self.freq = np.where(cnt > 0, pos / np.maximum(cnt, 1), 0.0)
        self.C2 = defaultdict(lambda: np.zeros(S))
        for e in tr_eps:
            for i in range(1, len(e) - 1): self.C2[(e[i - 1], e[i])][e[i + 1]] += 1
        self.cache = {}

    def risk2(self, a, b):
        key = (a, b)
        if key in self.cache: return self.cache[key]
        dist = {(a, b): 1.0}; hit = 0.0
        for _ in range(K):
            nd = defaultdict(float)
            for (x, y), pr in dist.items():
                row = self.C2.get((x, y))
                if row is None or row.sum() == 0: nd[(y, y)] += pr; continue
                r = row.copy(); r[:3] += SMOOTH; r /= r.sum()
                for z in range(S):
                    if r[z] == 0: continue
                    if z == D: hit += pr * r[z]
                    else: nd[(y, z)] += pr * r[z]
            dist = nd
        self.cache[key] = hit
        return hit

def score_into(acc, m, test_eps):
    for e in test_eps:
        for i, y in labels(e, K):
            s = e[i]; a = e[i - 1] if i > 0 else s
            acc["Y"].append(y); acc["state"].append(s)
            acc["Markov chain (1st order)"].append(m.risk[s])
            acc["Constant hazard"].append(m.hz)
            acc["Per-state frequency"].append(m.freq[s])
            acc["Markov chain (2nd order)"].append(m.risk2(a, s))

NAMES = ["Markov chain (1st order)", "Constant hazard", "Per-state frequency", "Markov chain (2nd order)"]

def summarize(acc):
    Y = np.array(acc["Y"]); res = {}
    for n in NAMES:
        p = np.array(acc[n]); res[n] = {"brier": brier(Y, p), "logloss": logloss(Y, p), "auc": auc(Y, p)}
    base = float(Y.mean()) if len(Y) else 0.0
    res["Always predict base rate"] = {"brier": brier(Y, np.full(len(Y), base)),
                                       "logloss": logloss(Y, np.full(len(Y), base)), "auc": 0.5}
    pm = np.array(acc["Markov chain (1st order)"]); st = np.array(acc["state"])
    edges = [0, 0.1, 0.2, 0.35, 0.5, 0.65, 0.8, 0.9, 1.0001]
    cal = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        mk = (pm >= lo) & (pm < hi)
        if mk.sum() > 0: cal.append({"bin": f"{lo:.2f}-{min(hi,1):.2f}", "n": int(mk.sum()),
                                     "predicted": float(pm[mk].mean()), "observed": float(Y[mk].mean())})
    per_state = []
    for s_ in range(3):
        mk = st == s_
        if mk.sum(): per_state.append({"state": s_, "n": int(mk.sum()), "predicted": float(pm[mk].mean()),
                                       "observed": float(Y[mk].mean())})
    return {"n": int(len(Y)), "base_rate": base, "metrics": res, "calibration": cal, "per_state": per_state}

def table(title, r):
    names = ["NORMAL", "CONTENTION", "HIGH_RISK"]
    L = [f"## {title}", "", f"{r['n']} scored points, base rate of 'deadlock within k' = {r['base_rate']:.3f}", "",
         "| model | Brier | log loss | AUC |", "|---|---|---|---|"]
    for n, m in r["metrics"].items(): L.append(f"| {n} | {m['brier']:.4f} | {m['logloss']:.4f} | {m['auc']:.3f} |")
    L += ["", "Calibration of the 1st-order chain:", "", "| predicted bin | n | mean predicted | observed |", "|---|---|---|---|"]
    for c in r["calibration"]: L.append(f"| {c['bin']} | {c['n']} | {c['predicted']:.3f} | {c['observed']:.3f} |")
    L += ["", "| state | n | mean predicted | observed |", "|---|---|---|---|"]
    for c in r["per_state"]: L.append(f"| {names[c['state']]} | {c['n']} | {c['predicted']:.3f} | {c['observed']:.3f} |")
    return L + [""]

def new_acc(): return {k: [] for k in ["Y", "state"] + NAMES}

def main():
    runs = load(PREFIX + "_samples.csv")
    ids = sorted(runs)
    ep_by_run = {r: episodes(runs[r]) for r in ids}

    # --- 3-fold cross-validation by run
    acc = new_acc()
    for f in range(3):
        test = [r for r in ids if r % 3 == f]; train = [r for r in ids if r % 3 != f]
        m = Models([e for r in train for e in ep_by_run[r]])
        score_into(acc, m, [e for r in test for e in ep_by_run[r]])
    cv = summarize(acc)

    # --- model on all training runs
    all_eps = [e for r in ids for e in ep_by_run[r]]
    full = Models(all_eps)
    C, P = full.C, full.P
    E, SD = absorbing_stats(P)
    ev = np.zeros(3); ex = np.zeros(3)
    for e in all_eps:
        for i in range(len(e)):
            if e[i] == D: continue
            s = e[i]; j = i
            while j < len(e) and e[j] != D: j += 1
            ex[s] += (j - i); ev[s] += 1 if j < len(e) else 0
    emp = [float(ex[s] / ev[s]) if ev[s] else None for s in range(3)]
    py_risk = curve(P, K)

    # --- fresh held-out dataset (never used for fitting or for choosing the classifier thresholds)
    held = None; held_cv = None
    if HELDOUT and os.path.exists(HELDOUT + "_samples.csv"):
        hr = load(HELDOUT + "_samples.csv")
        acc2 = new_acc()
        score_into(acc2, full, [e for r in sorted(hr) for e in episodes(hr[r])])
        held = summarize(acc2); held["runs"] = len(hr)
        # Same regime on both sides: cross-validate by run inside the light-load set alone.
        hids = sorted(hr); hep = {r: episodes(hr[r]) for r in hids}
        acc3 = new_acc()
        for f in range(3):
            tr = [r for r in hids if r % 3 != f]; te = [r for r in hids if r % 3 == f]
            score_into(acc3, Models([e for r in tr for e in hep[r]]), [e for r in te for e in hep[r]])
        held_cv = summarize(acc3)

    cpp = None
    cli = "build/markov_cli"
    if os.path.exists(cli):
        args = [cli] + [str(C[i, j]) for i in range(3) for j in range(4)] + [str(SMOOTH), str(K)]
        out = subprocess.run(args, capture_output=True, text=True).stdout.split("\n")
        cpp = []
        for line in out:
            if line.strip():
                a = line.split(); cpp.append({"state": int(a[0]), "E": float(a[1]), "sd": float(a[2]), "risk": float(a[3])})

    report = {"horizon_samples": K, "runs": len(ids), "cross_validation": cv, "heldout": held, "heldout_same_regime_cv": held_cv,
              "counts": C[:3].tolist(), "matrix": P.tolist(),
              "expected_steps": [None if np.isinf(x) else float(x) for x in E],
              "std_steps": [None if np.isinf(x) else float(x) for x in SD], "empirical_mean_steps": emp,
              "risk_within_horizon": [float(x) for x in py_risk[:3]], "cpp_crosscheck": cpp}
    json.dump(report, open("analysis/validation.json", "w"), indent=2)

    names = ["NORMAL", "CONTENTION", "HIGH_RISK"]
    L = [f"# Predictor validation (horizon k = {K} samples = {K*10} ms)", "",
         f"Training data: {len(ids)} runs. Sample = 10 ms. Same estimator as `src/predict/markov_predictor.cpp`.", ""]
    L += table(f"3-fold cross-validation by run ({len(ids)} runs)", cv)
    if held: L += table(f"Fresh held-out dataset ({held['runs']} runs, light-load profile, new seeds)", held)
    if held_cv: L += table("Light-load set, trained and tested in the SAME regime (3-fold CV by run)", held_cv)
    L += ["## Chain fitted on all training runs", "", "Counts (rows NORMAL, CONTENTION, HIGH_RISK; columns NORMAL, CONTENTION, HIGH_RISK, DEADLOCK):", "", "```"]
    for i in range(3): L.append("  ".join(f"{int(C[i, j]):7d}" for j in range(4)))
    L += ["```", "", "| state | expected steps to deadlock | std dev | empirical mean (censored) | P(deadlock within k) |", "|---|---|---|---|---|"]
    for s in range(3):
        e = "inf" if np.isinf(E[s]) else f"{E[s]:.1f}"; sd = "inf" if np.isinf(SD[s]) else f"{SD[s]:.1f}"
        em = "n/a" if emp[s] is None else f"{emp[s]:.1f}"
        L.append(f"| {names[s]} | {e} | {sd} | {em} | {py_risk[s]:.3f} |")
    if cpp:
        L += ["", "C++ cross-check (`build/markov_cli` on the same counts): " +
              ", ".join(f"{names[c['state']]} E={c['E']:.3f} risk={c['risk']:.4f}" for c in cpp)]
    open("analysis/validation_report.md", "w").write("\n".join(L) + "\n")
    print("\n".join(L))

if __name__ == "__main__":
    main()
