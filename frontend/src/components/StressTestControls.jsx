import React, { useState } from 'react';
import { runStressTest, forceDeadlock } from '../api/client';

export function StressTestControls() {
  const [threads, setThreads] = useState(4);
  const [transfers, setTransfers] = useState(10);
  const [mode, setMode] = useState('REACTIVE_DFS');
  const [statusMsg, setStatusMsg] = useState(null);
  const [loading, setLoading] = useState(false);

  const handleLaunchStress = async () => {
    setLoading(true);
    try {
      const res = await runStressTest(threads, transfers, mode);
      setStatusMsg(res.message || 'Stress workload running in background!');
    } catch (err) {
      setStatusMsg(`Error: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  const handleForceDeadlock = async () => {
    setLoading(true);
    try {
      const res = await forceDeadlock();
      setStatusMsg('Triggered forced reverse-lock deadlock scenario (ACC001↔ACC002)! Check graph above.');
    } catch (err) {
      setStatusMsg(`Error: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="glass-panel">
      <h3 className="text-lg font-semibold text-white mb-4 flex items-center gap-2">
        <span>⚡</span> Multi-Thread Stress Test & Deadlock Demo
      </h3>

      <div className="grid grid-cols-3 gap-3 mb-4">
        <div>
          <label className="block text-xs font-medium text-gray-400 mb-1">Simulated Threads</label>
          <input
            type="number"
            min="1"
            max="16"
            value={threads}
            onChange={(e) => setThreads(e.target.value)}
            className="input-field code-font"
          />
        </div>

        <div>
          <label className="block text-xs font-medium text-gray-400 mb-1">Transfers / Thread</label>
          <input
            type="number"
            min="1"
            max="100"
            value={transfers}
            onChange={(e) => setTransfers(e.target.value)}
            className="input-field code-font"
          />
        </div>

        <div>
          <label className="block text-xs font-medium text-gray-400 mb-1">Policy</label>
          <select
            value={mode}
            onChange={(e) => setMode(e.target.value)}
            className="input-field cursor-pointer text-xs"
          >
            <option value="REACTIVE_DFS">Reactive 2PL DFS</option>
            <option value="BANKERS_AVOIDANCE">Banker's Avoidance</option>
            <option value="PREDICTIVE_HYBRID">Predictive ML</option>
          </select>
        </div>
      </div>

      <div className="grid grid-cols-2 gap-3">
        <button
          onClick={handleLaunchStress}
          disabled={loading}
          className="btn-primary text-xs justify-center py-2"
        >
          🚀 Launch Concurrent Stress Test
        </button>

        <button
          onClick={handleForceDeadlock}
          disabled={loading}
          className="btn-danger text-xs justify-center py-2"
        >
          💥 Force Deadlock Scenario
        </button>
      </div>

      {statusMsg && (
        <div className="mt-3 text-xs bg-blue-950/40 border border-blue-800 text-blue-300 p-2.5 rounded">
          {statusMsg}
        </div>
      )}
    </div>
  );
}
