import React from 'react';
import { NavLink, Navigate, Outlet, Route, Routes, useLocation, useOutletContext } from 'react-router-dom';
import { useBank } from './hooks/useBank.js';
import Header from './components/Header.jsx';
import WaitForGraph from './components/WaitForGraph.jsx';
import Accounts from './components/Accounts.jsx';
import Controls from './components/Controls.jsx';
import Logs from './components/Logs.jsx';
import Predictor from './components/Predictor.jsx';
import Benchmark from './components/Benchmark.jsx';
import Toasts from './components/Toasts.jsx';

const PAGES = [
  ['/', 'Overview', true],
  ['/operate', 'Operate', false],
  ['/predictor', 'Predictor', false],
  ['/logs', 'Logs', false],
  ['/benchmark', 'Benchmark', false],
];

function Layout() {
  const b = useBank();
  const { pathname } = useLocation();
  React.useEffect(() => {
    const p = PAGES.find(([to]) => to === pathname);
    document.title = `${p ? p[1] : 'Page'} · PRIDE-Bank`;
  }, [pathname]);
  return (
    <>
      <Header source={b.source} pref={b.pref} setPref={b.setPref} base={b.base} setBase={b.setBase} mode={b.mode} setMode={b.setMode} />
      <nav className="nav" aria-label="Pages">
        {PAGES.map(([to, label, end]) => (
          <NavLink key={to} to={to} end={end} className={({ isActive }) => (isActive ? 'on' : '')}>
            {label}
          </NavLink>
        ))}
      </nav>
      {b.source === 'offline' && (
        <p className="banner" role="alert">
          The backend at {b.base} is not answering. Start PRIDE-Bank, or switch the data source to Demo.
        </p>
      )}
      <main className="page">
        <Outlet context={b} />
      </main>
      <Toasts toasts={b.toasts} dismiss={b.dismiss} />
    </>
  );
}

const useB = () => useOutletContext();

function Overview() {
  const b = useB();
  const { accounts, wfg } = b.data;
  return (
    <div className="grid-main">
      <WaitForGraph wfg={wfg} dl={b.data.dl} accountIds={accounts.map((a) => a.id)} />
      <Accounts accounts={accounts} locks={wfg.locks} />
    </div>
  );
}

function Operate() {
  const b = useB();
  const { accounts, wfg } = b.data;
  return (
    <div className="grid-side">
      <div className="stack">
        <WaitForGraph wfg={wfg} dl={b.data.dl} accountIds={accounts.map((a) => a.id)} />
        <Accounts accounts={accounts} locks={wfg.locks} />
      </div>
      <Controls accounts={accounts} busy={b.busy} actions={b.actions} lastTransfer={b.lastTransfer} mode={b.mode}
        priority={b.priority} setPriority={b.setPriority} policy={b.data.policy} />
    </div>
  );
}

function PredictorPage() {
  const b = useB();
  return <Predictor predictor={b.data.predictor} telemetry={b.data.telemetry} mode={b.mode} busy={b.busy} actions={b.actions} />;
}

function LogsPage() {
  const { data } = useB();
  return <Logs txns={data.txns} wal={data.wal} dl={data.dl} />;
}

export default function App() {
  return (
    <Routes>
      <Route element={<Layout />}>
        <Route index element={<Overview />} />
        <Route path="operate" element={<Operate />} />
        <Route path="predictor" element={<PredictorPage />} />
        <Route path="logs" element={<LogsPage />} />
        <Route path="benchmark" element={<Benchmark />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Route>
    </Routes>
  );
}
