import React from 'react';
import { usePolling } from './hooks/usePolling';
import { 
  fetchAccounts, 
  fetchWaitForGraph, 
  fetchWalLogs, 
  fetchDeadlockLogs 
} from './api/client';

import { AccountLedger } from './components/AccountLedger';
import { WaitForGraph } from './components/WaitForGraph';
import { TransactionManager } from './components/TransactionManager';
import { RecoveryManager } from './components/RecoveryManager';
import { RightRailTabs } from './components/RightRailTabs';

export default function App() {
  const { data: accounts, error: acctError } = usePolling(fetchAccounts, 800);
  const { data: graphData } = usePolling(fetchWaitForGraph, 500);
  const { data: walLogs } = usePolling(fetchWalLogs, 800);
  const { data: deadlockLogs } = usePolling(fetchDeadlockLogs, 800);

  const isConnected = !acctError && accounts !== null;
  const locksCount = Object.keys(graphData?.locks || {}).length;
  const activeThreadsCount = (graphData?.nodes || []).length;

  return (
    <div className="app">
      {/* Top Bar */}
      <div className="topbar">
        <div className="brand">
          <span className="mark">PRIDE</span>
          <h1>PRIDE-Bank</h1>
          <span className="tag">/ Priority-Based Resolution and Intelligent Deadlock Elimination in Banking</span>
        </div>
        
      </div>

      {/* Main Grid Layout */}
      <div className="layout">
        {/* Left: Accounts Ledger */}
        <AccountLedger accounts={accounts || []} locks={graphData?.locks || {}} />

        {/* Center Console */}
        <main className="console">
          <WaitForGraph graphData={graphData || {}} />
          <TransactionManager accounts={accounts || []} />
          <RecoveryManager />
        </main>

        {/* Right Rail Tabs */}
        <RightRailTabs
          walLogs={walLogs || []}
          deadlockLogs={deadlockLogs || []}
          accounts={accounts || []}
        />
      </div>

      
      
    </div>
  );
}
