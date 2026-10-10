-- Wipes everything (run by hand: mysql -u root -p banksafe < sql/drop_all.sql)
DROP TABLE IF EXISTS DeadlockLog;
DROP TABLE IF EXISTS TransactionLog;
DROP TABLE IF EXISTS Transactions;
DROP TABLE IF EXISTS Accounts;
