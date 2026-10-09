import React, { useState } from 'react';
import { executeTransfer } from '../api/client';

export function TransferForm({ accounts, onTransferComplete }) {
  const [from, setFrom] = useState('ACC001');
  const [to, setTo] = useState('ACC002');
  const [amount, setAmount] = useState('150.00');
  const [mode, setMode] = useState('REACTIVE_DFS');
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState(null);

  const handleSubmit = async (e) => {
    e.preventDefault();
    if (from === to) {
      setResult({ success: false, message: 'Source and Target accounts cannot be identical.' });
      return;
    }

    setLoading(true);
    setResult(null);

    try {
      const res = await executeTransfer(from, to, amount, mode);
      setResult(res);
      if (onTransferComplete) onTransferComplete();
    } catch (err) {
      setResult({ success: false, message: err.message });
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="glass-panel">
      <h3 className="text-lg font-semibold text-white mb-4 flex items-center gap-2">
        <span>💸</span> Execute Fund Transfer
      </h3>

      <form onSubmit={handleSubmit} className="space-y-4">
        <div className="grid grid-cols-2 gap-4">
          <div>
            <label className="block text-xs font-medium text-gray-400 mb-1">Source Account</label>
            <select
              value={from}
              onChange={(e) => setFrom(e.target.value)}
              className="input-field cursor-pointer"
            >
              {accounts.map((a) => (
                <option key={a.id} value={a.id} className="bg-gray-900">
                  {a.id} ({a.owner_name} - ${a.balance})
                </option>
              ))}
            </select>
          </div>

          <div>
            <label className="block text-xs font-medium text-gray-400 mb-1">Target Account</label>
            <select
              value={to}
              onChange={(e) => setTo(e.target.value)}
              className="input-field cursor-pointer"
            >
              {accounts.map((a) => (
                <option key={a.id} value={a.id} className="bg-gray-900">
                  {a.id} ({a.owner_name} - ${a.balance})
                </option>
              ))}
            </select>
          </div>
        </div>

        <div className="grid grid-cols-2 gap-4">
          <div>
            <label className="block text-xs font-medium text-gray-400 mb-1">Amount ($)</label>
            <input
              type="number"
              step="0.01"
              value={amount}
              onChange={(e) => setAmount(e.target.value)}
              className="input-field code-font"
              required
            />
          </div>

          <div>
            <label className="block text-xs font-medium text-gray-400 mb-1">Concurrency Policy</label>
            <select
              value={mode}
              onChange={(e) => setMode(e.target.value)}
              className="input-field cursor-pointer"
            >
              <option value="REACTIVE_DFS" className="bg-gray-900">Phase 2: Reactive DFS 2PL</option>
              <option value="BANKERS_AVOIDANCE" className="bg-gray-900">Phase 2: Banker's Avoidance</option>
              <option value="PREDICTIVE_HYBRID" className="bg-gray-900">Phase 3: Predictive ML Risk</option>
            </select>
          </div>
        </div>

        <button
          type="submit"
          disabled={loading}
          className="btn-primary w-full justify-center text-sm py-2.5"
        >
          {loading ? 'Processing Transfer...' : 'Initiate Secure Transfer'}
        </button>
      </form>

      {result && (
        <div className={`mt-4 p-3 rounded-lg text-sm border ${
          result.success ? 'bg-emerald-950/40 border-emerald-800 text-emerald-300' : 'bg-rose-950/40 border-rose-800 text-rose-300'
        }`}>
          <div className="font-semibold flex items-center justify-between">
            <span>{result.success ? '✓ Transfer Successful' : '✗ Transfer Aborted'}</span>
            {result.txnId && <span className="code-font text-xs opacity-75">Txn #{result.txnId}</span>}
          </div>
          <div className="text-xs mt-1">{result.message}</div>
          {result.deadlockDetected && (
            <div className="mt-2 pt-2 border-t border-rose-800/50 text-xs text-rose-400">
              ⚠️ Deadlock detected! Victim transaction ID {result.victimTxnId} was aborted.
            </div>
          )}
        </div>
      )}
    </div>
  );
}
