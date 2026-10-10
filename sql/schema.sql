-- PRIDE-Bank schema (MySQL 8.0+, InnoDB, 3NF)
-- Original design: Vandana Haryal. MySQL port: AUTO_INCREMENT instead of AUTOINCREMENT,
-- named CHECK constraints (enforced from MySQL 8.0.16), InnoDB tables.
--
-- IDEMPOTENT: safe to run on every start. It never drops anything, so balances and
-- the WAL survive a restart (the old script dropped all four tables on every start,
-- which made crash recovery impossible). Use sql/drop_all.sql for a clean slate.

CREATE TABLE IF NOT EXISTS Accounts (
    id VARCHAR(50) NOT NULL PRIMARY KEY,
    owner_name VARCHAR(100) NOT NULL,
    balance DOUBLE NOT NULL,
    version INT NOT NULL DEFAULT 1,
    CONSTRAINT chk_accounts_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS Transactions (
    txn_id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    from_acct VARCHAR(50) NOT NULL,
    to_acct VARCHAR(50) NOT NULL,
    amount DOUBLE NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_txn_amount CHECK (amount > 0),
    CONSTRAINT fk_txn_from FOREIGN KEY (from_acct) REFERENCES Accounts(id),
    CONSTRAINT fk_txn_to FOREIGN KEY (to_acct) REFERENCES Accounts(id)
) ENGINE=InnoDB;

-- The write-ahead log itself
CREATE TABLE IF NOT EXISTS TransactionLog (
    lsn BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    txn_id INT NOT NULL,
    op VARCHAR(20) NOT NULL,
    before_val TEXT,
    after_val TEXT,
    ts DATETIME DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_log_txn (txn_id)
) ENGINE=InnoDB;

-- Deadlock audit trail
CREATE TABLE IF NOT EXISTS DeadlockLog (
    dl_id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    victim_txn_id INT NOT NULL,
    cycle_accounts TEXT NOT NULL,
    detected_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
