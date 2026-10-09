import React from 'react';

export function WaitForGraph({ graphData = {} }) {
  const { nodes = [], edges = [], locks = {}, hasCycle = false, cycle = [] } = graphData;

  const cx = 320;
  const cy = 150;
  const r = 105;

  const pointFor = (i, total) => {
    const ang = -Math.PI / 2 + (i / Math.max(total, 1)) * Math.PI * 2;
    return [cx + r * Math.cos(ang), cy + r * Math.sin(ang)];
  };

  const nodePositions = {};
  nodes.forEach((id, i) => {
    nodePositions[id] = pointFor(i, nodes.length);
  });

  return (
    <section className="panel graph-wrap">
      <h2>
        <span className="chip" style={{ background: 'var(--lock)' }}></span>
        Wait-For Graph{' '}
        <span style={{ color: 'var(--faint)', textTransform: 'none', letterSpacing: 0 }}>
          — live lock &amp; dependency state {hasCycle ? '(DEADLOCK CYCLE DETECTED)' : ''}
        </span>
      </h2>

      <svg id="graphSvg" viewBox="0 0 640 300">
        <defs>
          <marker id="arrow-lock" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="var(--lock)" />
          </marker>
          <marker id="arrow-wait" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="var(--txn)" />
          </marker>
          <marker id="arrow-cycle" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="var(--deadlock)" />
          </marker>
        </defs>

        {/* Render Edges */}
        {edges.map((e, idx) => {
          const start = nodePositions[e.waitingTxn];
          const end = nodePositions[e.holdingTxn];
          if (!start || !end) return null;

          const isCycleEdge = cycle.includes(e.waitingTxn) && cycle.includes(e.holdingTxn);
          const state = isCycleEdge ? 'cycle' : 'wait';

          const [x1, y1] = start;
          const [x2, y2] = end;
          const dx = x2 - x1;
          const dy = y2 - y1;
          const len = Math.sqrt(dx * dx + dy * dy) || 1;
          const pad = 26;
          const sx = x1 + (dx / len) * pad;
          const sy = y1 + (dy / len) * pad;
          const ex = x2 - (dx / len) * pad;
          const ey = y2 - (dy / len) * pad;
          const mx = (sx + ex) / 2;
          const my = (sy + ey) / 2;

          return (
            <g key={idx}>
              <line
                x1={sx}
                y1={sy}
                x2={ex}
                y2={ey}
                stroke={isCycleEdge ? 'var(--deadlock)' : 'var(--txn)'}
                strokeWidth={isCycleEdge ? 2.6 : 1.8}
                strokeDasharray={isCycleEdge ? 'none' : '4 3'}
                markerEnd={`url(#arrow-${state})`}
              />
              <text
                x={mx}
                y={my - 6}
                fill="var(--muted)"
                fontSize="9.5"
                fontFamily="var(--mono)"
                textAnchor="middle"
              >
                {e.account}
              </text>
            </g>
          );
        })}

        {/* Render Nodes */}
        {nodes.map((nodeId) => {
          const pos = nodePositions[nodeId];
          if (!pos) return null;
          const [x, y] = pos;
          const isInCycle = cycle.includes(nodeId);

          return (
            <g key={nodeId}>
              <circle
                cx={x}
                cy={y}
                r="20"
                fill="var(--panel-2)"
                stroke={isInCycle ? 'var(--deadlock)' : 'var(--client)'}
                strokeWidth={isInCycle ? '2.5' : '1.6'}
              />
              <text
                x={x}
                y={y + 4}
                textAnchor="middle"
                fill="var(--text)"
                fontSize="12"
                fontFamily="var(--mono)"
                fontWeight="600"
              >
                T{nodeId}
              </text>
            </g>
          );
        })}

        {nodes.length === 0 && (
          <text
            x="320"
            y="150"
            textAnchor="middle"
            fill="var(--faint)"
            fontSize="12"
            fontFamily="var(--mono)"
          >
            no active locks or waits
          </text>
        )}
      </svg>

      <div className="legend">
        <span>
          <i style={{ background: 'var(--lock)' }}></i>holds lock
        </span>
        <span>
          <i style={{ background: 'var(--txn)', opacity: 0.55 }}></i>waiting for lock
        </span>
        <span>
          <i style={{ background: 'var(--deadlock)' }}></i>cycle detected
        </span>
        <span>
          <i style={{ background: 'var(--recovery)' }}></i>committed this tick
        </span>
      </div>
    </section>
  );
}
