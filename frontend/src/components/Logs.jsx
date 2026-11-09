import React, { useState } from 'react';
import { clock, money, plain, tid } from '../format.js';

const TABS = [
  ['txns', 'Transactions'],
  ['wal', 'Write-ahead log'],
  ['dl', 'Deadlocks'],
];

const Status = ({ s }) => <span className={`chip st-${String(s).toLowerCase()}`}>{String(s).replace('_', ' ').toLowerCase()}</span>;

export default function Logs({ txns, wal, dl }) {
  const [tab, setTab] = useState('txns');
  const counts = { txns: txns.length, wal: wal.length, dl: dl.length };

  return (
    <section className="panel" aria-labelledby="h-log">
      <div className="panel-head">
        <h2 id="h-log">Logs</h2>
        <div className="tabs" role="tablist" aria-label="Log type">
          {TABS.map(([id, label]) => (
            <button key={id} role="tab" aria-selected={tab === id} className={tab === id ? 'on' : ''} onClick={() => setTab(id)}>
              {label} <span className="count">{counts[id]}</span>
            </button>
          ))}
        </div>
      </div>

      <div className="scroll-y" role="tabpanel">
        {tab === 'txns' && (
          <table className="tbl">
            <thead>
              <tr><th>Txn</th><th>From</th><th>To</th><th className="r">Amount</th><th>Status</th><th>Time</th></tr>
            </thead>
            <tbody>
              {txns.map((t) => (
                <tr key={t.txn_id}>
                  <td className="mono">{tid(t.txn_id)}</td>
                  <td className="mono">{t.from_acct}</td>
                  <td className="mono">{t.to_acct}</td>
                  <td className="r">{money(t.amount)}</td>
                  <td><Status s={t.status} /></td>
                  <td className="mono muted">{clock(t.created_at)}</td>
                </tr>
              ))}
              {!txns.length && <tr><td colSpan="6" className="empty">No transactions yet. Send a transfer to create the first one.</td></tr>}
            </tbody>
          </table>
        )}

        {tab === 'wal' && (
          <table className="tbl">
            <thead>
              <tr><th>LSN</th><th>Txn</th><th>Operation</th><th className="r">Before</th><th className="r">After</th><th>Time</th></tr>
            </thead>
            <tbody>
              {wal.map((w) => (
                <tr key={w.lsn}>
                  <td className="mono">{w.lsn}</td>
                  <td className="mono">{tid(w.txn_id)}</td>
                  <td><span className={`op op-${String(w.op).toLowerCase()}`}>{w.op}</span></td>
                  <td className="r">{plain(w.before_val)}</td>
                  <td className="r">{plain(w.after_val)}</td>
                  <td className="mono muted">{clock(w.ts)}</td>
                </tr>
              ))}
              {!wal.length && <tr><td colSpan="6" className="empty">The log is empty. Every transfer writes BEGIN, DEBIT, CREDIT and COMMIT here first.</td></tr>}
            </tbody>
          </table>
        )}

        {tab === 'dl' && (
          <table className="tbl">
            <thead>
              <tr><th>ID</th><th>Victim</th><th>Accounts in the cycle</th><th>Detected</th></tr>
            </thead>
            <tbody>
              {dl.map((d) => (
                <tr key={d.dl_id}>
                  <td className="mono">{d.dl_id}</td>
                  <td className="mono">{tid(d.victim_txn_id)}</td>
                  <td className="mono">{String(d.cycle_accounts).split(',').join(', ')}</td>
                  <td className="mono muted">{clock(d.detected_at)}</td>
                </tr>
              ))}
              {!dl.length && <tr><td colSpan="4" className="empty">No deadlocks yet. Use “Force deadlock” to create one.</td></tr>}
            </tbody>
          </table>
        )}
      </div>
    </section>
  );
}
