import React from 'react';
import { clock, tid } from '../format.js';

const W = 640;
const H = 372;
const CX = 320;
const CY = 140;
const R = 24;

function layout(ids) {
  const n = ids.length;
  const pos = {};
  if (n === 1) pos[ids[0]] = [CX, CY];
  else {
    const start = n === 2 ? Math.PI : -Math.PI / 2;
    const rad = n === 2 ? 130 : 108;
    ids.forEach((id, i) => {
      const a = start + (i * 2 * Math.PI) / n;
      pos[id] = [CX + rad * Math.cos(a), CY + rad * Math.sin(a)];
    });
  }
  return pos;
}

function edgePath(p1, p2) {
  const dx = p2[0] - p1[0];
  const dy = p2[1] - p1[1];
  const len = Math.hypot(dx, dy) || 1;
  const ux = dx / len;
  const uy = dy / len;
  const s = [p1[0] + ux * (R + 2), p1[1] + uy * (R + 2)];
  const e = [p2[0] - ux * (R + 7), p2[1] - uy * (R + 7)];
  const nx = -uy;
  const ny = ux;
  const c = [(s[0] + e[0]) / 2 + nx * 36, (s[1] + e[1]) / 2 + ny * 36];
  const lx = 0.25 * s[0] + 0.5 * c[0] + 0.25 * e[0] + nx * 12;
  const ly = 0.25 * s[1] + 0.5 * c[1] + 0.25 * e[1] + ny * 12;
  return { d: `M${s[0]},${s[1]} Q${c[0]},${c[1]} ${e[0]},${e[1]}`, lx, ly };
}

export default function WaitForGraph({ wfg, dl, accountIds }) {
  const edges = wfg.edges || [];
  const locks = wfg.locks || {};
  const cycle = wfg.cycle || [];
  const idSet = new Set([...(wfg.nodes || []), ...Object.values(locks)]);
  edges.forEach((e) => {
    idSet.add(e.waitingTxn);
    idSet.add(e.holdingTxn);
  });
  const ids = [...idSet].sort((a, b) => a - b);
  const pos = layout(ids);

  const slots = accountIds.length ? accountIds : ['ACC001', 'ACC002', 'ACC003', 'ACC004', 'ACC005'];
  const sw = 112;
  const gap = 10;
  const x0 = (W - slots.length * sw - (slots.length - 1) * gap) / 2;

  const last = dl[0];
  const summary = wfg.hasCycle
    ? `Deadlock: ${[...cycle, cycle[0]].map(tid).join(' waits for ')}.`
    : ids.length
      ? `${ids.length} transaction${ids.length === 1 ? '' : 's'} active, none waiting in a cycle.`
      : 'No transactions are holding or waiting for locks.';

  return (
    <section className="panel graph" aria-labelledby="h-wfg">
      <div className="panel-head">
        <h2 id="h-wfg">Wait-for graph</h2>
        <span className="meta">
          <b className="num">{wfg.deadlocksResolved ?? 0}</b> deadlocks resolved
        </span>
      </div>

      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label={summary} className="wfg">
        <defs>
          <marker id="ah" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto">
            <path d="M0,0 L10,5 L0,10 z" className="arrow" />
          </marker>
          <marker id="ahd" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto">
            <path d="M0,0 L10,5 L0,10 z" className="arrow danger" />
          </marker>
        </defs>

        {ids.length === 0 && (
          <g className="hint">
            <text x={CX} y={CY - 6} textAnchor="middle">Nothing is waiting on a lock.</text>
            <text x={CX} y={CY + 16} textAnchor="middle" className="sub">Run “Force deadlock” to watch a cycle form.</text>
          </g>
        )}

        {/* lock ownership: transaction -> the account it holds */}
        {slots.map((id, i) => {
          const holder = locks[id];
          if (!holder || !pos[holder]) return null;
          const [px, py] = pos[holder];
          return (
            <line key={`h-${id}`} x1={px} y1={py + R} x2={x0 + i * (sw + gap) + sw / 2} y2={298} className="holds" />
          );
        })}

        {edges.map((e, i) => {
          const p1 = pos[e.waitingTxn];
          const p2 = pos[e.holdingTxn];
          if (!p1 || !p2) return null;
          const inCycle = cycle.includes(e.waitingTxn) && cycle.includes(e.holdingTxn);
          const { d, lx, ly } = edgePath(p1, p2);
          return (
            <g key={i} className={inCycle ? 'edge danger' : 'edge'}>
              <path d={d} markerEnd={`url(#${inCycle ? 'ahd' : 'ah'})`} />
              <text x={lx} y={ly} textAnchor="middle" className="elabel">{e.account}</text>
            </g>
          );
        })}

        {ids.map((id) => {
          const inCycle = cycle.includes(id);
          const [x, y] = pos[id];
          return (
            <g key={id} className={inCycle ? 'node danger' : 'node'}>
              <circle cx={x} cy={y} r={R} />
              <text x={x} y={y + 4} textAnchor="middle">{tid(id)}</text>
            </g>
          );
        })}

        {slots.map((id, i) => {
          const holder = locks[id];
          const x = x0 + i * (sw + gap);
          return (
            <g key={id} className={holder ? 'slot held' : 'slot'}>
              <rect x={x} y={298} width={sw} height={52} rx="6" />
              <text x={x + 12} y={320} className="sid">{id}</text>
              <text x={x + 12} y={340} className="sstate">{holder ? `held by ${tid(holder)}` : 'free'}</text>
            </g>
          );
        })}
      </svg>

      <div className="graph-foot">
        <p className={wfg.hasCycle ? 'status danger' : 'status'} role="status">
          {summary}
        </p>
        <div className="last">
          <span className="lbl">Last resolved</span>
          {last ? (
            <span>
              Deadlock #{last.dl_id}: <b className="mono">T{last.victim_txn_id}</b> aborted, cycle over{' '}
              <span className="mono">{String(last.cycle_accounts).split(',').join(', ')}</span> at {clock(last.detected_at)}
            </span>
          ) : (
            <span className="muted">None yet.</span>
          )}
        </div>
      </div>
    </section>
  );
}
