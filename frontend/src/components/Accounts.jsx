import React, { useEffect, useRef, useState } from 'react';
import { money, tid } from '../format.js';

const OPENING_TOTAL = 28000.75; // sum of sql/seed_data.sql

function Balance({ value }) {
  const prev = useRef(value);
  const [flash, setFlash] = useState('');
  useEffect(() => {
    if (prev.current !== value) {
      setFlash(value > prev.current ? 'up' : 'down');
      prev.current = value;
      const t = setTimeout(() => setFlash(''), 1400);
      return () => clearTimeout(t);
    }
    return undefined;
  }, [value]);
  return <span className={`bal ${flash}`}>{money(value)}</span>;
}

export default function Accounts({ accounts, locks }) {
  const total = accounts.reduce((s, a) => s + a.balance, 0);
  const max = Math.max(1, ...accounts.map((a) => a.balance));
  const diff = Math.round((total - OPENING_TOTAL) * 100) / 100;

  return (
    <section className="panel" aria-labelledby="h-acc">
      <div className="panel-head">
        <h2 id="h-acc">Accounts</h2>
        <span className="meta">InnoDB rows, one lock each</span>
      </div>
      <div className="scroll-x">
        <table className="tbl acc">
          <thead>
            <tr>
              <th>Account</th>
              <th>Owner</th>
              <th className="r">Balance</th>
              <th className="r">Version</th>
              <th>Lock</th>
            </tr>
          </thead>
          <tbody>
            {accounts.map((a) => {
              const holder = locks?.[a.id];
              return (
                <tr key={a.id}>
                  <td className="mono">{a.id}</td>
                  <td>{a.owner_name}</td>
                  <td className="r">
                    <Balance value={a.balance} />
                    <span className="bar" aria-hidden="true">
                      <i style={{ width: `${(a.balance / max) * 100}%` }} />
                    </span>
                  </td>
                  <td className="r mono">{a.version}</td>
                  <td>{holder ? <span className="chip lock">held by {tid(holder)}</span> : <span className="muted">free</span>}</td>
                </tr>
              );
            })}
            {accounts.length === 0 && (
              <tr>
                <td colSpan="5" className="empty">Waiting for account data.</td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
      <p className={`total ${diff === 0 ? '' : 'off'}`} role="status">
        <b>{money(total)}</b> in the system.{' '}
        {diff === 0
          ? 'Matches the opening total, so no money was created or lost.'
          : `${money(Math.abs(diff))} ${diff < 0 ? 'below' : 'above'} the opening total. A debit without a commit shows up here until recovery runs.`}
      </p>
    </section>
  );
}
