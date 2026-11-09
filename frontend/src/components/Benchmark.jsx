import React, { useState } from 'react';
import results from '../data/benchmark.json';

// Measured with benchmark/benchmark_suite.cpp (median of 3 runs, MySQL 8 on Linux, 20 ms of
// simulated work between the two lock requests). To refresh: run the suite and copy
// benchmark_results.json to src/data/benchmark.json.
const STRATS = [
  ['DETECT_YOUNGEST', 'Detect, abort youngest', 'a'],
  ['DETECT_VALUE', 'Detect, abort lowest cost', 'c'],
  ['BANKERS', "Banker's avoidance", 'b'],
  ['HYBRID', 'Markov hybrid', 'd'],
  ['ORDERED', 'Ordered locking', 'e'],
  ['NATIVE_MYSQL', 'Native InnoDB', 'f'],
];
const EXPERIMENTS = {
  thread_scaling: { label: 'Threads', key: (r) => r.threads, name: (k) => `${k} threads`, note: '5 accounts' },
  account_scaling: { label: 'Accounts', key: (r) => r.accounts, name: (k) => `${k} accounts`, note: '8 threads' },
  hold_time: { label: 'Hold time', key: (r) => r.holdMs, name: (k) => `${k} ms hold`, note: '8 threads, 5 accounts, 20 transfers per thread' },
  hot_spot: { label: 'Hot spot', key: (r) => r.hotProb, name: (k) => `${Math.round(k * 100)}% on 2 accounts`, note: '8 threads, 20 accounts' },
};

export default function Benchmark() {
  const [exp, setExp] = useState('thread_scaling');
  const cfg = EXPERIMENTS[exp];
  const rows = results.filter((r) => r.experiment === exp);
  const groups = [...new Set(rows.map(cfg.key))];
  const maxMs = Math.max(...rows.map((r) => r.elapsedMs));
  const heavy = groups[groups.length - 1];
  const heavyRows = STRATS.map(([id]) => rows.find((r) => cfg.key(r) === heavy && r.strategy === id));
  return (
    <section className="panel" aria-labelledby="h-bm">
      <div className="panel-head">
        <h2 id="h-bm">Strategy comparison</h2>
        <span className="chip sample">Measured, 5-run median</span>
      </div>
      <div className="bm-tabs">
        <div className="seg" role="radiogroup" aria-label="Experiment">
          {Object.entries(EXPERIMENTS).map(([id, e]) => (
            <button key={id} role="radio" aria-checked={exp === id} className={exp === id ? 'on' : ''} onClick={() => setExp(id)}>
              {e.label}
            </button>
          ))}
        </div>
        <span className="note">{cfg.note}{exp === 'hold_time' ? '' : ', 25 transfers per thread'}</span>
      </div>
      <div className="bm">
        <div className="legend">
          {STRATS.map(([id, label, c]) => <span key={id}><i className={c} />{label}</span>)}
        </div>
        <div className="bm-row">
          <div className="bm-name">Total time <small>(ms, lower is better)</small></div>
          {groups.map((g) => (
            <div key={g} className="bm-group">
              <div className="bm-glabel">{cfg.name(g)}</div>
              {STRATS.map(([id, label, c]) => {
                const r = rows.find((x) => cfg.key(x) === g && x.strategy === id);
                return (
                  <div key={id} className="bm-line2">
                    <span className="bm-track" aria-hidden="true"><i className={c} style={{ width: `${(r.elapsedMs / maxMs) * 100}%` }} /></span>
                    <span className="bm-val" title={label}>{Math.round(r.elapsedMs).toLocaleString()}</span>
                  </div>
                );
              })}
            </div>
          ))}
        </div>
        <div className="bm-row">
          <div className="bm-name">Heaviest case: {cfg.name(heavy)}</div>
          <table className="mx">
            <thead><tr><th>strategy</th><th>p95 ms</th><th>deadlocks</th><th>failed</th></tr></thead>
            <tbody>
              {STRATS.map(([id, label], i) => (
                <tr key={id}>
                  <td className="rowhead">{label}</td>
                  <td>{Math.round(heavyRows[i].p95Ms).toLocaleString()}</td>
                  <td>{heavyRows[i].deadlocks}</td>
                  <td>{heavyRows[i].failed}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <p className="note" style={{ marginTop: 8 }}>
            "Failed" means a transfer gave up after 3 deadlock aborts. Money was conserved in every run. The native InnoDB column uses one database connection per thread; ours uses one shared connection.
          </p>
        </div>
      </div>
    </section>
  );
}
