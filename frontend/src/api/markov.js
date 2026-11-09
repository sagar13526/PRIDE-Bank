// Same absorbing-chain maths as src/predict/markov_predictor.cpp, used by the demo engine.
// States: 0 NORMAL, 1 CONTENTION, 2 HIGH_RISK, 3 DEADLOCK (absorbing).
export const STATES = ['NORMAL', 'CONTENTION', 'HIGH_RISK', 'DEADLOCK'];

export class Markov {
  constructor(smoothing = 0.5) {
    this.smoothing = smoothing;
    this.counts = Array.from({ length: 4 }, () => [0, 0, 0, 0]);
    this.prev = -1;
  }
  observe(s) {
    if (this.prev >= 0 && this.prev < 3) this.counts[this.prev][s] += 1;
    this.prev = s;
  }
  resetEpisode() {
    this.prev = -1;
  }
  total() {
    return this.counts.slice(0, 3).flat().reduce((a, b) => a + b, 0);
  }
  matrix() {
    const P = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 1]];
    for (let i = 0; i < 3; i++) {
      const n = this.counts[i].reduce((a, b) => a + b, 0);
      if (n <= 0) {
        P[i][i] = 1;
        continue;
      }
      const d = n + this.smoothing * 3;
      for (let j = 0; j < 4; j++) P[i][j] = (this.counts[i][j] + (j < 3 ? this.smoothing : 0)) / d;
    }
    return P;
  }
  // Expected steps and std dev to absorption; null = infinite (never reaches DEADLOCK).
  absorbing() {
    const P = this.matrix();
    const reach = [false, false, false, true];
    for (let ch = true; ch; ) {
      ch = false;
      for (let i = 0; i < 3; i++)
        if (!reach[i] && P[i].some((p, j) => p > 0 && reach[j])) (reach[i] = true), (ch = true);
    }
    const fin = [0, 1, 2].map((i) => reach[i]);
    for (let ch = true; ch; ) {
      ch = false;
      for (let i = 0; i < 3; i++)
        if (fin[i] && [0, 1, 2].some((j) => P[i][j] > 0 && !fin[j])) (fin[i] = false), (ch = true);
    }
    const idx = [0, 1, 2].filter((i) => fin[i]);
    const out = [null, null, null].map(() => ({ steps: null, sd: null }));
    const n = idx.length;
    if (!n) return out;
    // invert (I - Q) by Gauss-Jordan
    const A = idx.map((i, a) => idx.map((j, b) => (a === b ? 1 : 0) - P[i][j]).concat(idx.map((_, b) => (a === b ? 1 : 0))));
    for (let c = 0; c < n; c++) {
      let p = c;
      for (let r = c + 1; r < n; r++) if (Math.abs(A[r][c]) > Math.abs(A[p][c])) p = r;
      if (Math.abs(A[p][c]) < 1e-12) return out;
      [A[c], A[p]] = [A[p], A[c]];
      const d = A[c][c];
      A[c] = A[c].map((x) => x / d);
      for (let r = 0; r < n; r++) {
        if (r === c) continue;
        const f = A[r][c];
        A[r] = A[r].map((x, k) => x - f * A[c][k]);
      }
    }
    const N = A.map((row) => row.slice(n));
    const t = N.map((row) => row.reduce((a, b) => a + b, 0));
    idx.forEach((i, a) => {
      const nt = N[a].reduce((s, v, b) => s + v * t[b], 0);
      out[i] = { steps: t[a], sd: Math.sqrt(Math.max(0, 2 * nt - t[a] - t[a] * t[a])) };
    });
    return out;
  }
  within(state, k) {
    const P = this.matrix();
    let v = [0, 0, 0, 0];
    v[state] = 1;
    for (let s = 0; s < k; s++) {
      const w = [0, 0, 0, 0];
      for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) w[j] += v[i] * P[i][j];
      v = w;
    }
    return v[3];
  }
}
