import React from 'react';

export function WalLogPanel({ walLogs }) {
  if (!walLogs || walLogs.length === 0) {
    return (
      <div className="glass-panel text-center py-6 text-gray-400 text-sm">
        No Write-Ahead Log entries recorded yet.
      </div>
    );
  }

  return (
    <div className="glass-panel">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-lg font-semibold text-white flex items-center gap-2">
          <span>📜</span> Write-Ahead Log (WAL Audit Trail)
        </h3>
        <span className="badge badge-warning">LSN Ordered</span>
      </div>

      <div className="overflow-x-auto max-h-[320px]">
        <table className="w-full text-left border-collapse">
          <thead className="sticky top-0 bg-gray-900 border-b border-gray-700 text-xs text-gray-400 uppercase">
            <tr>
              <th className="py-2 px-3">LSN</th>
              <th className="py-2 px-3">Txn ID</th>
              <th className="py-2 px-3">Operation</th>
              <th className="py-2 px-3">Before Value</th>
              <th className="py-2 px-3">After Value</th>
              <th className="py-2 px-3">Timestamp</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-gray-800 text-xs code-font">
            {walLogs.map((log) => (
              <tr key={log.lsn} className="hover:bg-gray-800/40">
                <td className="py-2 px-3 text-amber-400 font-bold">#{log.lsn}</td>
                <td className="py-2 px-3 text-blue-400">T{log.txn_id}</td>
                <td className="py-2 px-3 font-semibold">
                  <span className={`px-1.5 py-0.5 rounded text-[10px] ${
                    log.op === 'COMMIT' ? 'bg-emerald-950 text-emerald-300 border border-emerald-800' :
                    log.op === 'ABORT' || log.op === 'ROLLED_BACK' ? 'bg-rose-950 text-rose-300 border border-rose-800' :
                    'bg-blue-950 text-blue-300 border border-blue-800'
                  }`}>
                    {log.op}
                  </span>
                </td>
                <td className="py-2 px-3 text-gray-400">{log.before_val || '—'}</td>
                <td className="py-2 px-3 text-gray-300">{log.after_val || '—'}</td>
                <td className="py-2 px-3 text-gray-500">{log.ts}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
