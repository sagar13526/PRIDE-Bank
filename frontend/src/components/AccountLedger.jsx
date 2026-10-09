import React from 'react';

export function AccountLedger({ accounts = [], locks = {} }) {
  const accountNames = {
    ACC001: 'R. Sharma (Alice)',
    ACC002: 'K. Verma (Bob)',
    ACC003: 'M. Iyer (Charlie)',
    ACC004: 'S. Gupta (Diana)',
    ACC005: 'P. Nair (Evan)'
  };

  const totalBalance = accounts.reduce((sum, a) => sum + (parseFloat(a.balance) || 0), 0);
  const fmt = (n) => (parseFloat(n) || 0).toLocaleString('en-IN', { minimumFractionDigits: 2 });

  return (
    <aside className="panel">
      <h2>
        <span className="chip" style={{ background: 'var(--db)' }}></span>
        Accounts Ledger
      </h2>

      <div id="acctList">
        {accounts.map((acct) => {
          const isLocked = !!locks[acct.id];
          const owner = acct.owner_name || accountNames[acct.id] || 'Account Owner';

          return (
            <div key={acct.id} className={`acct-row ${isLocked ? 'locked' : ''}`}>
              <div>
                <span className="acct-id">{acct.id}</span>
                <span className="lock-ico">🔒</span>
                <span className="acct-owner">{owner}</span>
              </div>
              <div className="acct-bal">
                <span className="amt">₹{fmt(acct.balance)}</span>
                <span className="delta">v{acct.version}</span>
              </div>
            </div>
          );
        })}
      </div>

      <div className="ledger-foot">
        <span>{accounts.length} accounts</span>
        <span id="ledgerTotal">Σ ₹{fmt(totalBalance)}</span>
      </div>
    </aside>
  );
}
