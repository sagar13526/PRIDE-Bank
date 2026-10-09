import React, { useState } from 'react';
import { simulateCrash, simulateRecover, resetLedger } from '../api/client';

export function RecoveryManager({ onAction }) {
  const [crashStep, setCrashStep] = useState(0); // 0: running, 1: crash, 2: restart, 3: redo/undo, 4: consistent
  const [loading, setLoading] = useState(false);
  const [hasCrashLog, setHasCrashLog] = useState(false);

  const handleSimCrash = async () => {
    setLoading(true);
    setCrashStep(1);
    try {
      await simulateCrash();
      setHasCrashLog(true);
      if (onAction) onAction();
    } catch (err) {
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  const handleSimRecover = async () => {
    setLoading(true);
    setCrashStep(2);
    try {
      await new Promise((res) => setTimeout(res, 400));
      setCrashStep(3);
      await simulateRecover();
      await new Promise((res) => setTimeout(res, 500));
      setCrashStep(4);
      setHasCrashLog(false);
      if (onAction) onAction();
      setTimeout(() => setCrashStep(0), 1400);
    } catch (err) {
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  const handleReset = async () => {
    setLoading(true);
    setCrashStep(0);
    setHasCrashLog(false);
    try {
      await resetLedger();
      if (onAction) onAction();
    } catch (err) {
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  return (
    <section className="panel">
      <h2>
        <span className="chip" style={{ background: 'var(--recovery)' }}></span>
        Recovery Manager{' '}
        <span style={{ color: 'var(--faint)', textTransform: 'none', letterSpacing: 0 }}>
          — WAL &amp; crash recovery
        </span>
      </h2>

      <div className="row" style={{ gap: '10px' }}>
        <button className="danger" onClick={handleSimCrash} disabled={loading}>
          Kill process mid-transfer
        </button>
        <button className="good" onClick={handleSimRecover} disabled={loading || !hasCrashLog}>
          Restart &amp; replay WAL
        </button>
        <button onClick={handleReset} style={{ marginLeft: 'auto' }} disabled={loading}>
          Reset ledger
        </button>
      </div>

      <div className="crash-flow">
        <span className={`step ${crashStep === 0 ? 'active' : crashStep > 0 ? 'done' : ''}`}>
          running
        </span>
        <span className="arrow">→</span>
        <span className={`step ${crashStep === 1 ? 'active' : crashStep > 1 ? 'done' : ''}`}>
          crash
        </span>
        <span className="arrow">→</span>
        <span className={`step ${crashStep === 2 ? 'active' : crashStep > 2 ? 'done' : ''}`}>
          restart
        </span>
        <span className="arrow">→</span>
        <span className={`step ${crashStep === 3 ? 'active' : crashStep > 3 ? 'done' : ''}`}>
          redo/undo
        </span>
        <span className="arrow">→</span>
        <span className={`step ${crashStep === 4 ? 'active' : crashStep > 4 ? 'done' : ''}`}>
          consistent
        </span>
      </div>
    </section>
  );
}
