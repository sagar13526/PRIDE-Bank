# PRIDE-Bank

Concurrent bank-transfer engine in C++17: per-account locks with strict 2PL, Wait-For-Graph deadlock
detection (reactive) **or** Banker's-algorithm avoidance, write-ahead logging with crash recovery,
MySQL storage, REST API for the React dashboard.


## 1. Database (MySQL 8.0+)

```sql
-- as root:
CREATE USER 'pride_b'@'localhost' IDENTIFIED WITH mysql_native_password BY 'choose_a_password';
GRANT ALL ON `pride%`.* TO 'pride_b'@'localhost';
GRANT CREATE ON *.* TO 'pride_b'@'localhost';   -- lets the app create the database itself
```
(or create the `pride_bank` database yourself and grant only on it).

Connection settings come from environment variables (defaults in brackets):

| Variable | Meaning |
|---|---|
| `PRIDEBANK_DB_HOST` | [127.0.0.1] |
| `PRIDEBANK_DB_PORT` | [3306] |
| `PRIDEBANK_DB_USER` | [root] |
| `PRIDEBANK_DB_PASSWORD` | [empty] |
| `PRIDEBANK_DB_NAME` | [pride_bank] |

`sql/schema.sql` is idempotent and runs on every start (it never drops data). `sql/drop_all.sql` wipes the tables.

## 2. Build

```
sudo apt install build-essential cmake libmysqlclient-dev     # Linux
cmake -S . -B build && cmake --build build -j
```
Windows: easiest is MSYS2 (UCRT64): `pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,libmariadbclient}`, then `cmake -S . -B build -G Ninja`. Otherwise install MySQL Connector/C (or MariaDB Connector/C) and add
`-DMYSQL_INCLUDE_DIR=<dir with mysql.h> -DMYSQL_LIB=<path to libmysql/libmariadb>`.

`third_party/` holds cpp-httplib (`httplib.h`) and nlohmann/json — nothing else to install.

## 3. Run (from the project root, so `sql/` is found)

```
export PRIDEBANK_DB_USER=pride_b PRIDEBANK_DB_PASSWORD=...
./build/PRIDE-Bank            # API on http://localhost:8080  (optional arg: port)
```
On start it applies the schema, seeds ACC001-ACC005 if empty, and runs WAL crash recovery.

Windows PowerShell:

```
$env:PRIDEBANK_DB_USER="pride_b"; $env:PRIDEBANK_DB_PASSWORD="..."
.\build\PRIDE-Bank.exe
```

## 4. Dashboard (Node 18+)

```
cd dashboard
npm install
npm run dev                   # http://localhost:5173
```
It uses the backend on `http://localhost:8080` when it answers, otherwise it shows demo data.

## 5. Tests (use a throw-away database; the integration test DROPs its tables and refuses to run
##    unless the database name contains "test")

```
export PRIDEBANK_DB_USER=pride_b PRIDEBANK_DB_PASSWORD=...
./build/test_connector
./build/test_integration     # uses database pride_bank_test by default
```
