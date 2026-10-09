import React, { useState } from 'react';
import { executeTransfer, runStressTest, forceDeadlock } from '../api/client';

export function TransactionManager({ accounts = [], onAction }) {
  const [from, setFrom] = useState('ACC001');
  const [to, setTo] = useState('ACC002');
  const [amount, setAmount] = useState('50');
  const [threadCount, setThreadCount] = useState('10');
  const [loading, setLoading] = useState(false);
  const [readout, setReadout] = useState('idle.');

  const handleRunTransfer = async () => {
    if (from === to) {
      setReadout('Error: Source and Target accounts cannot be identical.');
      return;
    }
    setLoading(true);
    setReadout(`Executing single transfer: ${from} → ${to} (₹${amount})...`);
    try {
      const res = await executeTransfer(from, to, amount, 'REACTIVE_DFS');
      setReadout(res.message || (res.success ? 'Transfer committed successfully.' : 'Transfer aborted.'));
      if (onAction) onAction();
    } catch (err) {
      setReadout(`Error: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  const handleRunStress = async () => {
    setLoading(true);
    setReadout(`Spawning ${threadCount} concurrent transfer threads...`);
    try {
      const res = await runStressTest(threadCount, 5, 'REACTIVE_DFS');
      setReadout(res.message || `Launched ${threadCount} threads in background.`);
      if (onAction) onAction();
    } catch (err) {
      setReadout(`Error: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  const handleForceDeadlock = async () => {
    setLoading(true);
    setReadout('Forcing reverse-order deadlock (ACC001↔ACC002)...');
    try {
      const res = await forceDeadlock();
      setReadout('Triggered forced reverse-order deadlock scenario (A1→A2 / A2→A1)! Check graph & logs.');
      if (onAction) onAction();
    } catch (err) {
      setReadout(`Error: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  return (
    <section className="panel">
      <h2>
        <span className="chip" style={{ background: 'var(--txn)' }}></span>
        Transaction Manager
      </h2>

      <div className="controls">
        {/* Single Transfer Block */}
        <div className="ctrl-block">
          <h3>
            Single transfer <span className="tagline">BEGIN → 2PL lock → COMMIT</span>
          </h3>
          <div className="row">
            <select value={from} onChange={(e) => setFrom(e.target.value)}>
              {accounts.map((a) => (
                <option key={a.id} value={a.id}>
                  {a.id} — {a.owner_name}
                </option>
              ))}
            </select>
            <select value={to} onChange={(e) => setTo(e.target.value)}>
              {accounts.map((a) => (
                <option key={a.id} value={a.id}>
                  {a.id} — {a.owner_name}
                </option>
              ))}
            </select>
          </div>
          <div className="row">
            <input
              type="number"
              placeholder="amount"
              value={amount}
              min="1"
              onChange={(e) => setAmount(e.target.value)}
            />
            <button className="primary" onClick={handleRunTransfer} disabled={loading}>
              Run transfer
            </button>
          </div>
        </div>

        {/* Concurrency Simulation Block */}
        <div className="ctrl-block">
          <h3>
            Concurrency simulation <span className="tagline">std::thread client pool</span>
          </h3>
          <div className="row">
            <select value={threadCount} onChange={(e) => setThreadCount(e.target.value)}>
              <option value="4">4 threads</option>
              <option value="10">10 threads</option>
              <option value="25">25 threads</option>
            </select>
            <button className="primary" onClick={handleRunStress} disabled={loading}>
              Spawn &amp; run
            </button>
          </div>
          <div className="row">
            <button className="danger full" onClick={handleForceDeadlock} disabled={loading}>
              Force reverse-order deadlock (A→B / B→A)
            </button>
          </div>
          <div className="stress-readout">{readout}</div>
        </div>
      </div>
    </section>
  );
}
