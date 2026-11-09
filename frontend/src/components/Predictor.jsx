import React from 'react';

const NAMES = ['Normal', 'Contention', 'High risk', 'Deadlock'];
const pct = (x) => `${(x * 100).toFixed(x < 0.1 ? 1 : 0)}%`;
const time = (ms) => (ms == null ? 'never observed' : ms < 1000 ? `${Math.round(ms)} ms` : `${(ms / 1000).toFixed(1)} s`);

export default function Predictor({ predictor: p, telemetry, mode, busy, actions }) {
  if (!p) {
    return (
      <section className="panel" aria-labelledby="h-pred">
        <div className="panel-head"><h2 id="h-pred">Deadlock predictor</h2></div>
        <p className="note pad">The backend does not report a predictor. Rebuild it from the Phase 3 sources.</p>
      </section>
    );
  }
  const cur = p.currentState >= 0 ? p.currentState : 0;
  const learning = p.transitions < p.config.minSamples;
  const state = telemetry.length ? telemetry[telemetry.length - 1].state : cur;
  return (
    <section className="panel" aria-labelledby="h-pred">
      <div className="panel-head">
        <h2 id="h-pred">Deadlock predictor</h2>
        <span className={`chip ${p.avoiding ? 'lock' : ''}`}>
          {mode !== 'PREDICTIVE_HYBRID' ? 'Observing only' : p.avoiding ? "Using Banker's" : 'Using detection'}
        </span>
      </div>
      <div className="pred">
        <div>
          <div className="pred-now">
            <span className="pred-state" style={{ color: `var(--st${state})` }}>{NAMES[state]}</span>
            <span className="pred-risk">
              chance of a deadlock in the next {p.config.horizon * p.intervalMs} ms:{' '}
              <b>{learning ? 'still learning' : pct(p.risk)}</b>
            </span>
          </div>
          <h3>Load over the last {telemetry.length} samples</h3>
          <div className="strip" role="img" aria-label="Recent load states">
            {telemetry.map((t, i) => <i key={i} className={`s${t.state}`} title={NAMES[t.state]} />)}
          </div>
          <div className="legend-st">
            {NAMES.map((n, i) => <span key={n}><i style={{ background: `var(--st${i})` }} />{n}</span>)}
          </div>
          <p className="pred-cfg">
            Switches to Banker's at {pct(p.config.hi)} risk and back at {pct(p.config.lo)} ({p.switches} switches so far).
            Learned from {Math.round(p.transitions)} observed transitions, {Math.round(p.deadlockTransitions)} of them into a deadlock.
          </p>
          <div className="pred-actions">
            <button className="ghost" disabled={!!busy} onClick={actions.resetPredictor}>Forget what it learned</button>
          </div>
        </div>
        <div>
          <h3>Learned transition probabilities</h3>
          <table className="mx">
            <caption className="sr">Row = state now, column = state at the next sample</caption>
            <thead>
              <tr><th>from \ to</th>{NAMES.map((n) => <th key={n}>{n}</th>)}</tr>
            </thead>
            <tbody>
              {[0, 1, 2].map((i) => (
                <tr key={i}>
                  <td className="rowhead">{NAMES[i]}</td>
                  {p.matrix[i].map((v, j) => (
                    <td key={j} className="cell" style={{ '--p': v }} title={`${Math.round(p.counts[i][j])} observed`}>{v.toFixed(3)}</td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
          <h3 style={{ marginTop: 14 }}>Time to the next deadlock, from each state</h3>
          <table className="mx">
            <thead><tr><th>state</th><th>expected</th><th>± spread</th><th>risk in {p.config.horizon * p.intervalMs} ms</th></tr></thead>
            <tbody>
              {[0, 1, 2].map((i) => (
                <tr key={i}>
                  <td className="rowhead">{NAMES[i]}</td>
                  <td>{time(p.perState[i].expectedMs)}</td>
                  <td>{p.perState[i].stdDevSteps == null ? '-' : time(p.perState[i].stdDevSteps * p.intervalMs)}</td>
                  <td>{pct(p.perState[i].riskWithinHorizon)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  );
}
