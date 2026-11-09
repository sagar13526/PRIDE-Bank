import { Markov, STATES } from './markov.js';

// In-browser stand-in for the PRIDE-Bank C++ backend, used in Demo mode.
// It mirrors the real endpoints and JSON shapes, including the one awkward
// fact about the real engine: a deadlock cycle exists only for a moment
// before the victim is aborted, so the demo keeps it visible for ~2 seconds.

const SEED = [
  ['ACC001', 'Alice Johnson', 5000],
  ['ACC002', 'Bob Smith', 3500.5],
  ['ACC003', 'Charlie Brown', 10000],
  ['ACC004', 'Diana Prince', 7500.25],
  ['ACC005', 'Evan Wright', 2000],
];

let S;
let timers = [];

const round = (n) => Math.round(n * 100) / 100;
const ts = () => new Date().toISOString().slice(0, 19).replace('T', ' ');
const later = (ms, fn) => timers.push(setTimeout(fn, ms));
const acct = (id) => S.acc.find((a) => a.id === id);
const clone = (x) => JSON.parse(JSON.stringify(x));

function fresh() {
  timers.forEach(clearTimeout);
  timers = [];
  S = {
    acc: SEED.map(([id, owner_name, balance]) => ({ id, owner_name, balance, version: 1 })),
    txns: [],
    wal: [],
    dl: [],
    txn: 0,
    lsn: 0,
    dlid: 0,
    edges: [],
    nodes: [],
    locks: {},
    cycle: [],
    resolved: 0,
    active: 0,
    stress: false,
  };
}
fresh();

// ---- demo telemetry + predictor (same maths as the C++ MarkovPredictor) ----
const model = new Markov();
const cfg = { horizon: 5, hi: 0.6, lo: 0.35, minSamples: 30, enabled: true };
const P = { policy: 'YOUNGEST', avoiding: false, risk: 0, switches: 0, samples: 0, tele: [], lastResolved: 0, t0: Date.now() };
const classify = () => {
  if (S.resolved > P.lastResolved) return 3;
  if (S.cycle.length) return 2;
  if (S.active > 0 || S.stress) return S.stress && S.active > 0 ? 2 : 1;
  return 0;
};
setInterval(() => {
  const idle = S.active === 0 && !S.stress && S.resolved === P.lastResolved;
  const st = classify();
  P.lastResolved = S.resolved;
  P.samples++;
  if (idle) {
    model.resetEpisode();
    P.risk = 0;
    if (P.avoiding) { P.avoiding = false; P.switches++; }
  } else {
    model.observe(st);
    if (model.total() >= cfg.minSamples) {
      P.risk = model.within(st, cfg.horizon);
      if (!P.avoiding && P.risk >= cfg.hi) { P.avoiding = true; P.switches++; }
      else if (P.avoiding && P.risk <= cfg.lo) { P.avoiding = false; P.switches++; }
    }
  }
  P.tele.push({ t: (Date.now() - P.t0) / 1000, state: st, stateName: STATES[st], active: S.active, waiting: S.cycle.length ? 2 : 0, newDeadlocks: st === 3 ? 1 : 0 });
  if (P.tele.length > 400) P.tele.shift();
}, 100);

function predictorStatus() {
  const ab = model.absorbing();
  return {
    samples: P.samples,
    transitions: model.total(),
    deadlockTransitions: model.counts.slice(0, 3).reduce((a, r) => a + r[3], 0),
    currentState: model.prev,
    risk: P.risk,
    avoiding: P.avoiding,
    switches: P.switches,
    config: { ...cfg },
    intervalMs: 100,
    states: STATES,
    matrix: model.matrix(),
    counts: model.counts,
    perState: [0, 1, 2, 3].map((i) => ({
      expectedSteps: i < 3 ? ab[i].steps : 0,
      stdDevSteps: i < 3 ? ab[i].sd : 0,
      expectedMs: i < 3 && ab[i].steps != null ? ab[i].steps * 100 : i === 3 ? 0 : null,
      riskWithinHorizon: model.within(i, cfg.horizon),
      rowSamples: model.counts[i].reduce((a, b) => a + b, 0),
    })),
  };
}

const log = (txn_id, op, before_val = null, after_val = null) =>
  S.wal.unshift({ lsn: ++S.lsn, txn_id, op, before_val, after_val, ts: ts() });

const begin = (from, to, amount) => {
  const id = ++S.txn;
  S.txns.unshift({ txn_id: id, from_acct: from, to_acct: to, amount, status: 'PENDING', created_at: ts() });
  log(id, 'BEGIN');
  return id;
};
const setStatus = (id, status) => {
  const t = S.txns.find((x) => x.txn_id === id);
  if (t) t.status = status;
};
const debit = (id, a, amt) => {
  const x = acct(a);
  const before = x.balance;
  x.balance = round(before - amt);
  x.version++;
  log(id, 'DEBIT', before, x.balance);
};
const credit = (id, a, amt) => {
  const x = acct(a);
  const before = x.balance;
  x.balance = round(before + amt);
  x.version++;
  log(id, 'CREDIT', before, x.balance);
};
function commitTransfer(from, to, amt) {
  const id = begin(from, to, amt);
  debit(id, from, amt);
  credit(id, to, amt);
  log(id, 'COMMIT');
  setStatus(id, 'COMMITTED');
  return id;
}

// Two opposite transfers lock each other. After holdMs the "detector" picks the
// youngest transaction (largest id) as victim, aborts it, and it retries.
function episode(a, b, holdMs) {
  const t1 = begin(...a);
  const t2 = begin(...b);
  S.locks[a[0]] = t1;
  S.locks[b[0]] = t2;
  S.nodes = [t1, t2];
  S.active += 2;
  later(holdMs * 0.3, () => {
    S.edges = [
      { waitingTxn: t1, holdingTxn: t2, account: b[0] },
      { waitingTxn: t2, holdingTxn: t1, account: a[0] },
    ];
    S.cycle = [t1, t2];
  });
  later(holdMs, () => {
    const victim = Math.max(t1, t2);
    const other = victim === t1 ? t2 : t1;
    const vp = victim === t1 ? a : b;
    const op = victim === t1 ? b : a;
    S.dl.unshift({ dl_id: ++S.dlid, victim_txn_id: victim, cycle_accounts: `${a[0]},${b[0]}`, detected_at: ts() });
    S.resolved++;
    log(victim, 'ABORT');
    setStatus(victim, 'ABORTED');
    S.edges = [];
    S.cycle = [];
    delete S.locks[vp[0]];
    debit(other, op[0], op[2]);
    credit(other, op[1], op[2]);
    log(other, 'COMMIT');
    setStatus(other, 'COMMITTED');
    delete S.locks[op[0]];
    S.nodes = [];
    S.active -= 2;
    later(300, () => commitTransfer(...vp));
  });
}

const conflict = (message) => Object.assign(new Error(message), { status: 409 });

const routes = {
  'GET /api/predictor': () => predictorStatus(),
  'POST /api/predictor/reset': () => {
    model.counts = Array.from({ length: 4 }, () => [0, 0, 0, 0]);
    model.prev = -1;
    P.avoiding = false;
    P.risk = 0;
    return predictorStatus();
  },
  'GET /api/telemetry': () => ({ running: true, intervalMs: 100, samples: P.tele.slice(-150) }),
  'GET /api/policy': () => ({ victimPolicy: P.policy }),
  'POST /api/policy': ({ victimPolicy }) => {
    if (victimPolicy === 'YOUNGEST' || victimPolicy === 'VALUE_BASED') P.policy = victimPolicy;
    return { victimPolicy: P.policy };
  },
  'GET /api/accounts': () => S.acc,
  'GET /api/transactions': () => S.txns.slice(0, 50),
  'GET /api/wal-logs': () => S.wal.slice(0, 50),
  'GET /api/deadlock-logs': () => S.dl.slice(0, 50),
  'GET /api/wait-for-graph': () => ({
    nodes: S.nodes,
    edges: S.edges,
    locks: S.locks,
    hasCycle: S.cycle.length > 0,
    cycle: S.cycle,
    deadlocksResolved: S.resolved,
  }),

  'POST /api/transfer': ({ from, to, amount }) => {
    const amt = Number(amount);
    const fail = (message) => ({ success: false, txnId: -1, message, deadlockDetected: false, victimTxnId: -1, retries: 0 });
    if (!from || !to || from === to) return fail('Choose two different accounts.');
    if (!(amt > 0)) return fail('Amount must be greater than zero.');
    if (!acct(from) || !acct(to)) return fail('Unknown account.');
    if (acct(from).balance < amt) {
      const id = begin(from, to, amt);
      log(id, 'ABORT');
      setStatus(id, 'ABORTED');
      return { success: false, txnId: id, message: 'Insufficient funds.', deadlockDetected: false, victimTxnId: -1, retries: 0 };
    }
    const id = commitTransfer(from, to, amt);
    return { success: true, txnId: id, message: 'Transfer committed.', deadlockDetected: false, victimTxnId: -1, retries: 0 };
  },

  'POST /api/force-deadlock': () => {
    if (S.active > 0) throw conflict('A deadlock scenario is already running.');
    episode(['ACC001', 'ACC002', 100], ['ACC002', 'ACC001', 150], 2400);
    return { status: 'started', message: 'Forced deadlock scenario triggered.' };
  },

  'POST /api/stress-test': ({ threads = 4, transfers = 10, mode }) => {
    if (S.stress) throw conflict('A stress test is already running.');
    S.stress = true;
    const rounds = Math.max(10, Math.min(40, Math.round((threads * transfers) / 3)));
    let i = 0;
    const tick = () => {
      if (i++ >= rounds) {
        S.stress = false;
        return;
      }
      const ids = S.acc.map((a) => a.id);
      const from = ids[Math.floor(Math.random() * 5)];
      let to = ids[Math.floor(Math.random() * 5)];
      if (to === from) to = ids[(ids.indexOf(from) + 1) % 5];
      const amt = round(10 + Math.random() * 190);
      const avoid = mode === 'BANKERS_AVOIDANCE' || (mode === 'PREDICTIVE_HYBRID' && P.avoiding);
      if (!avoid && S.active === 0 && Math.random() < 0.16) {
        episode([from, to, amt], [to, from, round(amt * 0.8)], 1100);
      } else if (acct(from).balance >= amt) {
        commitTransfer(from, to, amt);
      }
      later(130, tick);
    };
    tick();
    return { status: 'started', message: 'Stress test launched in background.' };
  },

  'POST /api/simulate-crash': () => {
    if (S.active > 0) throw conflict('Transfers are in flight; try again when idle.');
    const id = begin('ACC003', 'ACC004', 200);
    debit(id, 'ACC003', 200);
    return { status: 'crashed', txnId: id, message: 'Simulated crash: DEBIT applied and WAL-logged, no COMMIT. Call /api/recover.' };
  },

  'POST /api/recover': () => {
    if (S.active > 0) throw conflict('Transfers are in flight; recovery would undo live transactions.');
    let n = 0;
    for (const t of [...S.txns]) {
      if (t.status !== 'PENDING') continue;
      for (const w of S.wal.filter((x) => x.txn_id === t.txn_id && x.op === 'DEBIT')) {
        const a = acct(t.from_acct);
        a.balance = round(Number(w.before_val));
        a.version++;
      }
      log(t.txn_id, 'ROLLED_BACK');
      setStatus(t.txn_id, 'ROLLED_BACK');
      n++;
    }
    return { status: 'recovered', recoveredTxns: n, message: 'WAL crash recovery complete.' };
  },

  'POST /api/reset': () => {
    if (S.active > 0) throw conflict('Transfers are in flight; try again when idle.');
    fresh();
    return { status: 'reset', message: 'Accounts and logs reset.' };
  },
};

export async function handle(method, path, body) {
  await new Promise((r) => setTimeout(r, 25));
  const route = routes[`${method} ${path.split('?')[0]}`];
  if (!route) throw new Error(`No such endpoint: ${path}`);
  return clone(route(body || {}));
}
