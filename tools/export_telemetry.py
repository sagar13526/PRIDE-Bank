#!/usr/bin/env python3
"""Export a telemetry dataset from MySQL to the CSV files analysis/validate_predictor.py reads.

    python3 tools/export_telemetry.py <dataset> <out_prefix>
    python3 tools/export_telemetry.py indep data/indep      # -> data/indep_samples.csv, data/indep_runs.csv

Connection: PRIDEBANK_DB_HOST / _PORT / _USER / _PASSWORD / _NAME (same variables as the C++ code).
Needs one of:  pip install pymysql   or   pip install mysql-connector-python
"""
import csv
import os
import sys


def connect():
    cfg = dict(host=os.environ.get("PRIDEBANK_DB_HOST", "127.0.0.1"),
               port=int(os.environ.get("PRIDEBANK_DB_PORT", "3306")),
               user=os.environ.get("PRIDEBANK_DB_USER", "root"),
               password=os.environ.get("PRIDEBANK_DB_PASSWORD", ""),
               database=os.environ.get("PRIDEBANK_DB_NAME", "pride_bank"))
    try:
        import pymysql
        return pymysql.connect(**cfg)
    except ImportError:
        pass
    try:
        import mysql.connector
        return mysql.connector.connect(**cfg)
    except ImportError:
        sys.exit("Install a MySQL driver first:  pip install pymysql")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    dataset, prefix = sys.argv[1], sys.argv[2]
    con = connect()
    cur = con.cursor()

    cur.execute("SELECT run_id, threads, accounts, hot_prob, transfers_per_thread, seed, deadlocks, "
                "elapsed_ms, committed, failed FROM telemetry_run WHERE dataset = %s ORDER BY run_id", (dataset,))
    runs = cur.fetchall()
    if not runs:
        sys.exit("No runs stored for dataset '%s'." % dataset)
    with open(prefix + "_runs.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["run", "threads", "accounts", "hot_prob", "transfers_per_thread", "seed",
                    "deadlocks", "elapsed_ms", "committed", "failed"])
        w.writerows(runs)

    cur.execute("SELECT run_id, t_sec, active, waiting, max_chain, locked, new_deadlocks, new_waits, state "
                "FROM telemetry_sample WHERE dataset = %s ORDER BY run_id, sample_id", (dataset,))
    n = 0
    with open(prefix + "_samples.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["run", "tSec", "active", "waiting", "maxChain", "locked", "newDeadlocks", "newWaits", "state"])
        for row in cur:
            w.writerow(row)
            n += 1
    con.close()
    print("Wrote %s_runs.csv (%d runs) and %s_samples.csv (%d samples)" % (prefix, len(runs), prefix, n))


if __name__ == "__main__":
    main()
