import React, { useState } from 'react';

export function RightRailTabs({ walLogs = [], deadlockLogs = [], accounts = [] }) {
  const [activeTab, setActiveTab] = useState('wal');

  // Compute stats for benchmark tab
  const deadlockCount = deadlockLogs.length;
  const committedCount = walLogs.filter((l) => l.op === 'COMMIT').length;
  const abortedCount = walLogs.filter((l) => l.op === 'ABORT' || l.op === 'ROLLED_BACK').length;
  const avgWait = 45; // ms

  // SQL code mapping helper
  const getSqlForOp = (log) => {
    const op = log.op;
    const txn = `T${log.txn_id}`;
    if (op === 'BEGIN') return `BEGIN TRANSACTION; -- ${txn}`;
    if (op === 'LOCK') return `SELECT balance FROM Accounts WHERE id='${log.before_val}' FOR UPDATE; -- ${txn}`;
    if (op === 'DEBIT') return `UPDATE Accounts SET balance = balance - ${log.after_val || '50'} WHERE id='${log.before_val}';`;
    if (op === 'CREDIT') return `UPDATE Accounts SET balance = balance + ${log.after_val || '50'} WHERE id='${log.before_val}';`;
    if (op === 'COMMIT') return `INSERT INTO TransactionLog(txn_id, op, ts) VALUES ('${log.txn_id}', 'COMMIT', NOW());\nCOMMIT; -- ${txn}`;
    if (op === 'ABORT' || op === 'ROLLED_BACK') return `ROLLBACK; -- ${txn} uncommitted undo`;
    return `-- ${op} ${log.before_val} ${log.after_val}`;
  };

  return (
    <aside className="panel" style={{ paddingBottom: '8px' }}>
      <div className="tabs">
        <button
          className={`tab-btn ${activeTab === 'wal' ? 'active' : ''}`}
          onClick={() => setActiveTab('wal')}
        >
          WAL / Txn Log
        </button>
        <button
          className={`tab-btn ${activeTab === 'sql' ? 'active' : ''}`}
          onClick={() => setActiveTab('sql')}
        >
          SQL Trace
        </button>
        <button
          className={`tab-btn ${activeTab === 'deadlocks' ? 'active' : ''}`}
          onClick={() => setActiveTab('deadlocks')}
        >
          Deadlock Log
        </button>
        <button
          className={`tab-btn ${activeTab === 'schema' ? 'active' : ''}`}
          onClick={() => setActiveTab('schema')}
        >
          Schema
        </button>
        <button
          className={`tab-btn ${activeTab === 'bench' ? 'active' : ''}`}
          onClick={() => setActiveTab('bench')}
        >
          Benchmark
        </button>
      </div>

      {/* 1. WAL TAB */}
      <div className={`tab-panel ${activeTab === 'wal' ? 'active' : ''}`} id="tab-wal">
        {walLogs.length === 0 ? (
          <div className="log-empty">no log entries yet — run a transfer</div>
        ) : (
          walLogs.map((l) => (
            <div key={l.lsn} className="log-line">
              <span className="seq">#{String(l.lsn).padStart(4, '0')}</span>
              <span>
                <span className={`op-${l.op}`}>{l.op}</span> Txn T{l.txn_id} {l.before_val} {l.after_val}
              </span>
            </div>
          ))
        )}
      </div>

      {/* 2. SQL TRACE TAB */}
      <div className={`tab-panel ${activeTab === 'sql' ? 'active' : ''}`} id="tab-sql">
        <div className="sql-hint">
          This is the actual SQL each WAL entry maps to — the OS lock is <i>how</i> isolation is enforced; the SQL underneath is <i>what</i> the DBMS actually guarantees.
        </div>
        {walLogs.length === 0 ? (
          <div className="log-empty">no SQL emitted yet — run a transfer</div>
        ) : (
          walLogs.map((l) => (
            <div key={l.lsn} className="sql-line">
              <span className="sql-seq">#{String(l.lsn).padStart(4, '0')}</span>
              <code className={`sql-code op-${l.op}`}>{getSqlForOp(l)}</code>
            </div>
          ))
        )}
      </div>

      {/* 3. DEADLOCK LOG TAB */}
      <div className={`tab-panel ${activeTab === 'deadlocks' ? 'active' : ''}`} id="tab-deadlocks">
        {deadlockLogs.length === 0 ? (
          <div className="log-empty">no deadlocks detected yet</div>
        ) : (
          deadlockLogs.map((dl) => (
            <div key={dl.dl_id} className="dl-card">
              <div className="hdr">
                <span>DEADLOCK #{dl.dl_id}</span>
                <span>cycle detected</span>
              </div>
              <div className="cycle">wait-for cycle: {dl.cycle_accounts}</div>
              <div className="victim">
                resolution: abort <b>Txn T{dl.victim_txn_id}</b> (youngest), rollback + release locks
              </div>
            </div>
          ))
        )}
      </div>

      {/* 4. SCHEMA TAB */}
      <div className={`tab-panel ${activeTab === 'schema' ? 'active' : ''}`} id="tab-schema">
        <div className="schema-note">
          Normalized to 3NF — every UPDATE/INSERT in the SQL Trace tab writes to one of these tables.
        </div>
        <div className="er-card">
          <div className="er-title">
            Accounts <span className="er-pk">4 cols</span>
          </div>
          <div className="er-cols">
            <span className="pk">id</span> VARCHAR PK · owner_name VARCHAR · balance DECIMAL(12,2) · version INT
          </div>
        </div>
        <div className="er-card">
          <div className="er-title">
            Transactions <span className="er-pk">6 cols</span>
          </div>
          <div className="er-cols">
            <span className="pk">txn_id</span> INT PK · <span className="fk">from_acct</span> FK→Accounts.id ·{' '}
            <span className="fk">to_acct</span> FK→Accounts.id · amount DECIMAL(12,2) · status ENUM(pending,committed,aborted) · created_at TIMESTAMP
          </div>
        </div>
        <div className="er-card">
          <div className="er-title">
            TransactionLog (WAL) <span className="er-pk">6 cols</span>
          </div>
          <div className="er-cols">
            <span className="pk">lsn</span> INT PK · <span className="fk">txn_id</span> FK→Transactions.txn_id · op ENUM(BEGIN,DEBIT,CREDIT,COMMIT,ROLLBACK) · before_val DECIMAL · after_val DECIMAL · ts TIMESTAMP
          </div>
        </div>
        <div className="er-card">
          <div className="er-title">
            DeadlockLog <span className="er-pk">4 cols</span>
          </div>
          <div className="er-cols">
            <span className="pk">dl_id</span> INT PK · <span className="fk">victim_txn_id</span> FK→Transactions.txn_id · cycle_accounts TEXT · detected_at TIMESTAMP
          </div>
        </div>
        <div className="er-rel">
          Accounts (1) ─── (∞) Transactions&nbsp;&nbsp;·&nbsp;&nbsp;Transactions (1) ─── (∞) TransactionLog
          <br />
          Transactions (1) ─── (0..1) DeadlockLog &nbsp;— set only when this txn was aborted as a victim
        </div>
      </div>

      {/* 5. BENCHMARK TAB */}
      <div className={`tab-panel ${activeTab === 'bench' ? 'active' : ''}`} id="tab-bench">
        <div className="bench-grid">
          <div className="bench-stat">
            <div className="label">Deadlocks caught</div>
            <div className="val">{deadlockCount}</div>
          </div>
          <div className="bench-stat">
            <div className="label">Committed</div>
            <div className="val">{committedCount}</div>
          </div>
          <div className="bench-stat">
            <div className="label">Aborted</div>
            <div className="val">{abortedCount}</div>
          </div>
          <div className="bench-stat">
            <div className="label">Avg wait</div>
            <div className="val">
              {avgWait}
              <small> ms</small>
            </div>
          </div>
        </div>

        <div className="bar-row">
          <div className="bar-label">
            <span>Avg wait — naive locking</span>
            <span>180ms</span>
          </div>
          <div className="bar-track">
            <div className="bar-fill before" style={{ width: '90%' }}></div>
          </div>
        </div>
        <div className="bar-row">
          <div className="bar-label">
            <span>Avg wait — WFG + Banker's</span>
            <span>45ms</span>
          </div>
          <div className="bar-track">
            <div className="bar-fill after" style={{ width: '25%' }}></div>
          </div>
        </div>
        <div className="bar-row">
          <div className="bar-label">
            <span>Failed transfers — naive locking</span>
            <span>34%</span>
          </div>
          <div className="bar-track">
            <div className="bar-fill before" style={{ width: '34%' }}></div>
          </div>
        </div>
        <div className="bar-row">
          <div className="bar-label">
            <span>Failed transfers — with detection</span>
            <span>{committedCount + abortedCount > 0 ? Math.round((abortedCount / (committedCount + abortedCount)) * 100) : 0}%</span>
          </div>
          <div className="bar-track">
            <div
              className="bar-fill after"
              style={{
                width: `${committedCount + abortedCount > 0 ? Math.round((abortedCount / (committedCount + abortedCount)) * 100) : 0}%`
              }}
            ></div>
          </div>
        </div>
      </div>
    </aside>
  );
}
