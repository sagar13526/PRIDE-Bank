import { useCallback, useEffect, useRef, useState } from 'react';
import { request, probe } from '../api/client.js';

const EMPTY = {
  accounts: [],
  txns: [],
  wal: [],
  dl: [],
  wfg: { nodes: [], edges: [], locks: {}, hasCycle: false, cycle: [], deadlocksResolved: 0 },
  predictor: null,
  telemetry: [],
  policy: 'YOUNGEST',
};

const load = (k, d) => {
  try {
    return localStorage.getItem(k) || d;
  } catch {
    return d;
  }
};
const save = (k, v) => {
  try {
    localStorage.setItem(k, v);
  } catch {
    /* storage unavailable */
  }
};

export function useBank() {
  const [pref, setPrefState] = useState(() => load('pride.pref', 'auto')); // auto | live | demo
  const [base, setBaseState] = useState(() => load('pride.base', 'http://localhost:8080'));
  const [source, setSource] = useState('probing'); // probing | live | demo | offline
  const [data, setData] = useState(EMPTY);
  const [mode, setMode] = useState('REACTIVE_DFS');
  const [busy, setBusy] = useState(null);
  const [toasts, setToasts] = useState([]);
  const [lastTransfer, setLastTransfer] = useState(null);
  const [priority, setPriority] = useState(5);

  const srcRef = useRef('probing');
  const failRef = useRef(0);
  const resolvedRef = useRef(null);
  const toastId = useRef(0);

  const setPref = (v) => {
    save('pride.pref', v);
    setPrefState(v);
  };
  const setBase = (v) => {
    save('pride.base', v);
    setBaseState(v);
  };

  const toast = useCallback((kind, text) => {
    const id = ++toastId.current;
    setToasts((t) => [...t.slice(-3), { id, kind, text }]);
    setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), 6000);
  }, []);
  const dismiss = (id) => setToasts((t) => t.filter((x) => x.id !== id));

  // Decide where data comes from.
  useEffect(() => {
    let cancelled = false;
    srcRef.current = 'probing';
    setSource('probing');
    resolvedRef.current = null;
    failRef.current = 0;
    (async () => {
      let next;
      if (pref === 'demo') next = 'demo';
      else if (await probe(base)) next = 'live';
      else next = pref === 'auto' ? 'demo' : 'offline';
      if (cancelled) return;
      srcRef.current = next;
      setSource(next);
    })();
    return () => {
      cancelled = true;
    };
  }, [pref, base]);

  const tick = useCallback(async () => {
    const kind = srcRef.current;
    if (kind === 'probing') return;
    const k = kind === 'demo' ? 'demo' : 'live';
    try {
      const [accounts, txns, wfg, wal, dl, predictor, tele, pol] = await Promise.all([
        request(k, base, 'GET', '/api/accounts'),
        request(k, base, 'GET', '/api/transactions'),
        request(k, base, 'GET', '/api/wait-for-graph'),
        request(k, base, 'GET', '/api/wal-logs'),
        request(k, base, 'GET', '/api/deadlock-logs'),
        request(k, base, 'GET', '/api/predictor').catch(() => null),
        request(k, base, 'GET', '/api/telemetry?n=120').catch(() => null),
        request(k, base, 'GET', '/api/policy').catch(() => null),
      ]);
      if (srcRef.current !== kind) return;
      failRef.current = 0;
      if (kind === 'offline') {
        srcRef.current = 'live';
        setSource('live');
      }
      const n = wfg.deadlocksResolved ?? 0;
      if (resolvedRef.current !== null && n > resolvedRef.current) {
        const v = dl[0];
        toast('warn', v ? `Deadlock resolved: T${v.victim_txn_id} was aborted and retried.` : 'Deadlock resolved.');
      }
      resolvedRef.current = n;
      setData({
        accounts, txns, wfg, wal, dl, predictor,
        telemetry: tele?.samples ?? [],
        policy: pol?.victimPolicy ?? 'YOUNGEST',
      });
    } catch {
      if (kind === 'demo') return;
      if (++failRef.current >= 3 && srcRef.current === 'live') {
        srcRef.current = 'offline';
        setSource('offline');
      }
    }
  }, [base, toast]);

  useEffect(() => {
    if (source === 'probing') return undefined;
    tick();
    const t = setInterval(tick, source === 'demo' ? 600 : 900);
    return () => clearInterval(t);
  }, [source, tick]);

  const act = useCallback(
    async (key, fn, onOk) => {
      setBusy(key);
      try {
        const r = await fn(srcRef.current === 'demo' ? 'demo' : 'live');
        onOk?.(r);
      } catch (e) {
        toast('err', e.message === 'Failed to fetch' ? 'Cannot reach the backend.' : e.message);
      } finally {
        setBusy(null);
        tick();
      }
    },
    [tick, toast],
  );

  const call = (k, method, path, body) => request(k, base, method, path, body);

  const actions = {
    transfer: (from, to, amount) =>
      act(
        'transfer',
        (k) => call(k, 'POST', '/api/transfer', { from, to, amount: Number(amount), mode, priority: Number(priority) }),
        (r) => {
          setLastTransfer(r);
          if (r.success) {
            toast(
              'ok',
              `T${r.txnId} committed${r.deadlockDetected ? ` after a deadlock (T${r.victimTxnId} aborted)` : ''}.`,
            );
          } else toast('err', r.message || 'Transfer failed.');
        },
      ),
    forceDeadlock: () =>
      act('deadlock', (k) => call(k, 'POST', '/api/force-deadlock'), () =>
        toast('info', 'Two opposite transfers started. Watch the graph.'),
      ),
    stress: (threads, transfers) =>
      act('stress', (k) => call(k, 'POST', '/api/stress-test', { threads, transfers, mode }), () =>
        toast('info', `Stress test started (${{ REACTIVE_DFS: 'detect and recover', BANKERS_AVOIDANCE: "Banker's", PREDICTIVE_HYBRID: 'Markov predictor' }[mode]}).`),
      ),
    crash: () =>
      act('crash', (k) => call(k, 'POST', '/api/simulate-crash'), (r) =>
        toast(r.status === 'crashed' ? 'warn' : 'err', r.message),
      ),
    recover: () =>
      act('recover', (k) => call(k, 'POST', '/api/recover'), (r) =>
        toast('ok', `Recovery finished: ${r.recoveredTxns} transaction${r.recoveredTxns === 1 ? '' : 's'} rolled back.`),
      ),
    setPolicy: (victimPolicy) =>
      act('policy', (k) => call(k, 'POST', '/api/policy', { victimPolicy }), (r) =>
        toast('ok', `Deadlock victim policy: ${r.victimPolicy === 'VALUE_BASED' ? 'lowest abort cost' : 'youngest'}.`),
      ),
    resetPredictor: () =>
      act('predreset', (k) => call(k, 'POST', '/api/predictor/reset'), () => toast('ok', 'Predictor forgot what it learned.')),
    reset: () => act('reset', (k) => call(k, 'POST', '/api/reset'), () => toast('ok', 'Accounts and logs reset.')),
  };

  return { data, source, pref, setPref, base, setBase, mode, setMode, busy, toasts, dismiss, lastTransfer, priority, setPriority, actions };
}
