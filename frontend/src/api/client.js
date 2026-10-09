const API_BASE_URL = 'http://localhost:8080/api';

export async function fetchAccounts() {
  const res = await fetch(`${API_BASE_URL}/accounts`);
  if (!res.ok) throw new Error('Failed to fetch accounts');
  return res.json();
}

export async function fetchTransactions() {
  const res = await fetch(`${API_BASE_URL}/transactions`);
  if (!res.ok) throw new Error('Failed to fetch transactions');
  return res.json();
}

export async function fetchWaitForGraph() {
  const res = await fetch(`${API_BASE_URL}/wait-for-graph`);
  if (!res.ok) throw new Error('Failed to fetch Wait-For graph');
  return res.json();
}

export async function fetchWalLogs() {
  const res = await fetch(`${API_BASE_URL}/wal-logs`);
  if (!res.ok) throw new Error('Failed to fetch WAL logs');
  return res.json();
}

export async function fetchDeadlockLogs() {
  const res = await fetch(`${API_BASE_URL}/deadlock-logs`);
  if (!res.ok) throw new Error('Failed to fetch deadlock logs');
  return res.json();
}

export async function executeTransfer(from, to, amount, mode = 'REACTIVE_DFS') {
  const res = await fetch(`${API_BASE_URL}/transfer`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ from, to, amount: parseFloat(amount), mode })
  });
  return res.json();
}

export async function runStressTest(threads, transfers = 5, mode = 'REACTIVE_DFS') {
  const res = await fetch(`${API_BASE_URL}/stress-test`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ threads: parseInt(threads), transfers: parseInt(transfers), mode })
  });
  return res.json();
}

export async function forceDeadlock() {
  const res = await fetch(`${API_BASE_URL}/force-deadlock`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' }
  });
  return res.json();
}

export async function simulateCrash() {
  const res = await fetch(`${API_BASE_URL}/simulate-crash`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' }
  });
  return res.json();
}

export async function simulateRecover() {
  const res = await fetch(`${API_BASE_URL}/recover`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' }
  });
  return res.json();
}

export async function resetLedger() {
  const res = await fetch(`${API_BASE_URL}/reset`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' }
  });
  return res.json();
}
