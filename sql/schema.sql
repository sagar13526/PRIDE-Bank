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

-- ---------------------------------------------------------------------------
-- Predictor data (Phase 3, data side of the Markov predictor)
-- A "dataset" is a named collection of runs ('live', 'train', 'heldout', 'indep', ...).
-- ---------------------------------------------------------------------------

-- One row per benchmark / recording run.
CREATE TABLE IF NOT EXISTS telemetry_run (
    dataset VARCHAR(40) NOT NULL,
    run_id INT NOT NULL,
    threads INT NULL,
    accounts INT NULL,
    hot_prob DOUBLE NULL,
    transfers_per_thread INT NULL,
    seed INT NULL,
    deadlocks INT NULL,
    elapsed_ms DOUBLE NULL,
    committed INT NULL,
    failed INT NULL,
    PRIMARY KEY (dataset, run_id)
) ENGINE=InnoDB;

-- One row per telemetry sample (every 10 ms). state: 0 NORMAL, 1 CONTENTION, 2 HIGH_RISK, 3 DEADLOCK.
CREATE TABLE IF NOT EXISTS telemetry_sample (
    sample_id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    dataset VARCHAR(40) NOT NULL,
    run_id INT NOT NULL,
    t_sec DOUBLE NOT NULL,
    active INT NOT NULL,
    waiting INT NOT NULL,
    max_chain INT NOT NULL,
    locked INT NOT NULL,
    new_deadlocks INT NOT NULL,
    new_waits INT NOT NULL,
    state TINYINT NOT NULL,
    CONSTRAINT chk_sample_state CHECK (state BETWEEN 0 AND 3),
    CONSTRAINT fk_sample_run FOREIGN KEY (dataset, run_id)
        REFERENCES telemetry_run (dataset, run_id) ON DELETE CASCADE,
    INDEX idx_sample_run (dataset, run_id, sample_id),
    INDEX idx_sample_state (dataset, state)
) ENGINE=InnoDB;

-- The learned transition counts of the Markov chain (one row per from/to pair per model).
CREATE TABLE IF NOT EXISTS markov_counts (
    model VARCHAR(40) NOT NULL,
    from_state TINYINT NOT NULL,
    to_state TINYINT NOT NULL,
    cnt DOUBLE NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (model, from_state, to_state),
    CONSTRAINT chk_counts_states CHECK (from_state BETWEEN 0 AND 2 AND to_state BETWEEN 0 AND 3),
    CONSTRAINT chk_counts_cnt CHECK (cnt >= 0)
) ENGINE=InnoDB;
