import React, { useEffect, useRef, useState } from 'react';

function Btn({ busy, id, children, busyText, ...p }) {
  return (
    <button {...p} disabled={p.disabled || !!busy} aria-busy={busy === id}>
      {busy === id ? busyText : children}
    </button>
  );
}

function ConfirmBtn({ onConfirm, busy, id, children }) {
  const [armed, setArmed] = useState(false);
  const t = useRef();
  useEffect(() => () => clearTimeout(t.current), []);
  return (
    <button
      className={armed ? 'danger' : 'ghost'}
      disabled={!!busy}
      onClick={() => {
        if (armed) {
          clearTimeout(t.current);
          setArmed(false);
          onConfirm();
        } else {
          setArmed(true);
          t.current = setTimeout(() => setArmed(false), 3000);
        }
      }}
    >
      {busy === id ? 'Resetting…' : armed ? 'Click again to erase everything' : children}
    </button>
  );
}

const MODE_TEXT = { REACTIVE_DFS: 'detect and recover', BANKERS_AVOIDANCE: "Banker's avoidance", PREDICTIVE_HYBRID: 'the Markov predictor' };

export default function Controls({ accounts, busy, actions, lastTransfer, mode, priority, setPriority, policy }) {
  const ids = accounts.length ? accounts.map((a) => a.id) : ['ACC001', 'ACC002'];
  const [from, setFrom] = useState('ACC001');
  const [to, setTo] = useState('ACC002');
  const [amount, setAmount] = useState('250');
  const [threads, setThreads] = useState(8);
  const [per, setPer] = useState(25);

  const same = from === to;
  const bad = !(Number(amount) > 0);

  return (
    <section className="panel controls" aria-labelledby="h-ctl">
      <div className="panel-head">
        <h2 id="h-ctl">Run the engine</h2>
      </div>

      <form
        className="group"
        onSubmit={(e) => {
          e.preventDefault();
          if (!same && !bad) actions.transfer(from, to, amount);
        }}
      >
        <h3>Transfer</h3>
        <div className="row2">
          <label className="field">
            <span>From</span>
            <select value={from} onChange={(e) => setFrom(e.target.value)}>
              {ids.map((i) => <option key={i}>{i}</option>)}
            </select>
          </label>
          <label className="field">
            <span>To</span>
            <select value={to} onChange={(e) => setTo(e.target.value)}>
              {ids.map((i) => <option key={i}>{i}</option>)}
            </select>
          </label>
        </div>
        <label className="field">
          <span>Amount</span>
          <input type="number" min="0.01" step="0.01" value={amount} onChange={(e) => setAmount(e.target.value)} inputMode="decimal" />
        </label>
        <label className="field">
          <span>Priority (0 lowest, 9 highest)</span>
          <select value={priority} onChange={(e) => setPriority(Number(e.target.value))}>
            {[0, 1, 2, 3, 4, 5, 6, 7, 8, 9].map((p) => <option key={p} value={p}>{p}</option>)}
          </select>
        </label>
        <Btn id="transfer" busy={busy} busyText="Sending…" className="primary" type="submit" disabled={same || bad}>
          Send transfer
        </Btn>
        <p className="note" role="status">
          {same
            ? 'Pick two different accounts.'
            : lastTransfer
              ? `${lastTransfer.success ? 'Committed' : 'Rejected'}: ${lastTransfer.message}${lastTransfer.retries ? ` (${lastTransfer.retries} retries)` : ''}`
              : `Uses ${MODE_TEXT[mode]}.`}
        </p>
      </form>

      <div className="group">
        <h3>Deadlock</h3>
        <p className="note">Two opposite transfers lock each other. The wait-for graph finds the cycle and aborts one of them.</p>
        <div className="seg" role="radiogroup" aria-label="Who is aborted">
          {[['YOUNGEST', 'Youngest'], ['VALUE_BASED', 'Lowest abort cost']].map(([id, label]) => (
            <button key={id} role="radio" aria-checked={policy === id} className={policy === id ? 'on' : ''}
              disabled={!!busy} onClick={() => policy !== id && actions.setPolicy(id)}
              title={id === 'VALUE_BASED' ? 'Priority, retries, age, amount and held locks decide who loses' : 'The transaction with the largest id loses'}>
              {label}
            </button>
          ))}
        </div>
        <Btn id="deadlock" busy={busy} busyText="Starting…" className="secondary" onClick={actions.forceDeadlock}>
          Force deadlock
        </Btn>
      </div>

      <div className="group">
        <h3>Load</h3>
        <div className="row2">
          <label className="field">
            <span>Threads</span>
            <input type="number" min="1" max="64" value={threads} onChange={(e) => setThreads(+e.target.value)} />
          </label>
          <label className="field">
            <span>Transfers each</span>
            <input type="number" min="1" max="1000" value={per} onChange={(e) => setPer(+e.target.value)} />
          </label>
        </div>
        <Btn id="stress" busy={busy} busyText="Starting…" className="secondary" onClick={() => actions.stress(threads, per)}>
          Run stress test
        </Btn>
      </div>

      <div className="group">
        <h3>Crash recovery</h3>
        <p className="note">Debit an account, skip the commit, then let the write-ahead log undo it.</p>
        <div className="row2">
          <Btn id="crash" busy={busy} busyText="Crashing…" className="secondary" onClick={actions.crash}>
            Simulate crash
          </Btn>
          <Btn id="recover" busy={busy} busyText="Recovering…" className="secondary" onClick={actions.recover}>
            Recover
          </Btn>
        </div>
      </div>

      <div className="group">
        <h3>Data</h3>
        <ConfirmBtn id="reset" busy={busy} onConfirm={actions.reset}>
          Reset accounts and logs
        </ConfirmBtn>
      </div>
    </section>
  );
}
