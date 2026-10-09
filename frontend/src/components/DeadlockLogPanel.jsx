import React from 'react';

export function DeadlockLogPanel({ deadlockLogs }) {
  if (!deadlockLogs || deadlockLogs.length === 0) {
    return (
      <div className="glass-panel text-center py-6 text-gray-400 text-sm">
        No deadlock cycle resolutions logged yet.
      </div>
    );
  }

  return (
    <div className="glass-panel">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-lg font-semibold text-white flex items-center gap-2">
          <span>☠️</span> Deadlock Audit Log (Victim Selection)
        </h3>
        <span className="badge badge-danger">{deadlockLogs.length} Cycles Resolved</span>
      </div>

      <div className="overflow-x-auto max-h-[320px]">
        <table className="w-full text-left border-collapse">
          <thead className="sticky top-0 bg-gray-900 border-b border-gray-700 text-xs text-gray-400 uppercase">
            <tr>
              <th className="py-2 px-3">DL ID</th>
              <th className="py-2 px-3">Sacrificed Victim Txn</th>
              <th className="py-2 px-3">Detected Cycle Nodes</th>
              <th className="py-2 px-3">Resolution Policy</th>
              <th className="py-2 px-3">Detected At</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-gray-800 text-xs code-font">
            {deadlockLogs.map((log) => (
              <tr key={log.dl_id} className="hover:bg-gray-800/40">
                <td className="py-2 px-3 text-rose-400 font-bold">#{log.dl_id}</td>
                <td className="py-2 px-3 text-rose-300 font-semibold">T{log.victim_txn_id}</td>
                <td className="py-2 px-3 text-yellow-300">[{log.cycle_accounts}]</td>
                <td className="py-2 px-3 text-gray-400">Phase 2 Naive Youngest</td>
                <td className="py-2 px-3 text-gray-500">{log.detected_at}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
