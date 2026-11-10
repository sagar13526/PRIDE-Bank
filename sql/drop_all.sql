-- Wipes everything (run by hand: mysql -u root -p pride_bank < sql/drop_all.sql)
DROP VIEW IF EXISTS v_transition_counts;
DROP VIEW IF EXISTS v_state_summary;
DROP VIEW IF EXISTS v_run_summary;
DROP TABLE IF EXISTS markov_counts;
DROP TABLE IF EXISTS telemetry_sample;
DROP TABLE IF EXISTS telemetry_run;
DROP TABLE IF EXISTS DeadlockLog;
DROP TABLE IF EXISTS TransactionLog;
DROP TABLE IF EXISTS Transactions;
DROP TABLE IF EXISTS Accounts;
