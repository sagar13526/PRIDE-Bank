import React from 'react';

const MODES = [
  { id: 'REACTIVE_DFS', label: 'Detect and recover', hint: 'Wait-for graph finds the cycle, youngest transaction is aborted' },
  { id: 'BANKERS_AVOIDANCE', label: "Avoid with Banker's", hint: 'A lock is granted only if the system stays in a safe state' },
  { id: 'PREDICTIVE_HYBRID', label: 'Predict with Markov', hint: "The Markov predictor switches to Banker's when deadlock risk is high" },
];

const STATUS = {
  probing: { cls: 'wait', text: 'Connecting' },
  live: { cls: 'ok', text: 'Live backend' },
  demo: { cls: 'demo', text: 'Demo data' },
  offline: { cls: 'err', text: 'Backend unreachable' },
};

export default function Header({ source, pref, setPref, base, setBase, mode, setMode }) {
  const st = STATUS[source];
  return (
    <header className="top">
      <div className="brand">
        <span className="logo" aria-hidden="true">
          <svg viewBox="0 0 24 24" width="22" height="22">
            <circle cx="6" cy="12" r="3.2" fill="none" stroke="currentColor" strokeWidth="2" />
            <circle cx="18" cy="12" r="3.2" fill="none" stroke="currentColor" strokeWidth="2" />
            <path d="M9.4 10.6 14.6 10.6M14.6 13.4 9.4 13.4" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
          </svg>
        </span>
        <div>
          <h1>PRIDE-Bank</h1>
          <p>Concurrent transfer engine</p>
        </div>
      </div>

      <div className="top-right">
        <div className="seg" role="radiogroup" aria-label="Deadlock strategy">
          {MODES.map((m) => (
            <button
              key={m.id}
              role="radio"
              aria-checked={mode === m.id}
              className={mode === m.id ? 'on' : ''}
              title={m.hint}
              onClick={() => setMode(m.id)}
            >
              {m.label}
            </button>
          ))}
        </div>

        <details className="conn">
          <summary className={`pill ${st.cls}`}>
            <i aria-hidden="true" />
            {st.text}
          </summary>
          <div className="conn-pop">
            <fieldset>
              <legend>Data source</legend>
              {[
                ['auto', 'Auto', 'Use the backend if it answers, otherwise demo data'],
                ['live', 'Live', 'Always use the backend'],
                ['demo', 'Demo', 'Simulated engine in this browser'],
              ].map(([v, l, d]) => (
                <label key={v} className="opt">
                  <input type="radio" name="src" checked={pref === v} onChange={() => setPref(v)} />
                  <span>
                    <b>{l}</b>
                    <small>{d}</small>
                  </span>
                </label>
              ))}
            </fieldset>
            <label className="field">
              <span>Backend address</span>
              <input value={base} onChange={(e) => setBase(e.target.value)} spellCheck="false" />
            </label>
          </div>
        </details>
      </div>
    </header>
  );
}
