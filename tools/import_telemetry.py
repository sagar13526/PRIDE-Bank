#!/usr/bin/env python3
"""Load a telemetry CSV pair (as written by `benchmark_suite --collect`) into MySQL.

    python3 tools/import_telemetry.py <dataset> <csv_prefix>
    python3 tools/import_telemetry.py indep data/indep      # reads data/indep_runs.csv and data/indep_samples.csv

The dataset is replaced if it already exists (safe to re-run). Needs the tables from sql/schema.sql.
Connection variables and drivers: see tools/export_telemetry.py.
"""
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_telemetry import connect  # noqa: E402


def rows(path):
    with open(path, newline="") as f:
        yield from csv.DictReader(f)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    dataset, prefix = sys.argv[1], sys.argv[2]
    con = connect()
    cur = con.cursor()
    cur.execute("DELETE FROM telemetry_run WHERE dataset = %s", (dataset,))   # samples cascade

    runs = [(dataset, int(r["run"]), int(r["threads"]), int(r["accounts"]), float(r["hot_prob"]),
             int(r["transfers_per_thread"]), int(r["seed"]), int(r["deadlocks"]), float(r["elapsed_ms"]),
             int(r["committed"]), int(r["failed"])) for r in rows(prefix + "_runs.csv")]
    cur.executemany("INSERT INTO telemetry_run (dataset, run_id, threads, accounts, hot_prob, "
                    "transfers_per_thread, seed, deadlocks, elapsed_ms, committed, failed) "
                    "VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)", runs)

    batch, total = [], 0
    sql = ("INSERT INTO telemetry_sample (dataset, run_id, t_sec, active, waiting, max_chain, locked, "
           "new_deadlocks, new_waits, state) VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)")
    for r in rows(prefix + "_samples.csv"):
        batch.append((dataset, int(r["run"]), float(r["tSec"]), int(r["active"]), int(r["waiting"]),
                      int(r["maxChain"]), int(r["locked"]), int(r["newDeadlocks"]), int(r["newWaits"]),
                      int(r["state"])))
        if len(batch) == 1000:
            cur.executemany(sql, batch)
            total += len(batch)
            batch = []
    if batch:
        cur.executemany(sql, batch)
        total += len(batch)
    con.commit()
    con.close()
    print("Loaded dataset '%s': %d runs, %d samples" % (dataset, len(runs), total))


if __name__ == "__main__":
    main()
