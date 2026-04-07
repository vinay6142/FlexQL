# FlexQL Compilation and Execution Instructions

This document lists compilation and execution commands supported by this project.

## 1) Prerequisites

- Linux/macOS environment
- `g++` with C++11 support
- `make`

Optional utilities used in troubleshooting/examples:
- `pkill`, `pgrep`, `ss`

---

## 2) Core Build Commands

### Build default target (same as `make all`)
```bash
make
```

### Build all major binaries/tests
```bash
make all
```

### Clean build artifacts and table data
```bash
make clean
```

---

## 3) Run Commands (Server/Client/Runner)

### Build and run server
```bash
make server
```

### Build and run client REPL
```bash
make client
```

### Build and run query runner binary
```bash
make query_runner
```

---

## 4) Test Commands

### Run all tests
```bash
make test
```

### Run component tests individually
```bash
make test_storage
make test_index
make test_executor
make test_planner
make test_parser
make test_resolver
make test_delete
make test_update
make test_persistence
```

---

## 5) Benchmark Commands

### Main benchmark
```bash
make benchmark
```

### Insert-focused benchmark
```bash
make benchmark_insert
```

---

## 6) Build-Only Commands for Specific Binaries

These commands compile targets without automatically running the phony wrappers.

```bash
make build/server
make build/client
make build/query_runner
make build/test_storage
make build/test_index
make build/test_executor
make build/test_planner
make build/test_parser
make build/test_resolver
make build/test_delete
make build/test_update
make build/test_persistence
make build/benchmark_flexql
make build/benchmark_insert
```

---

## 7) Direct Binary Execution Commands

After build, binaries can be run directly:

```bash
./build/server
./build/client
./build/query_runner
./build/test_storage
./build/test_index
./build/test_executor
./build/test_planner
./build/test_parser
./build/test_resolver
./build/test_delete
./build/test_update
./build/test_persistence
./build/benchmark_flexql
./build/benchmark_insert
```

---

## 8) Typical End-to-End Workflow

### Terminal 1 (server)
```bash
make build/server
./build/server
```

### Terminal 2 (client)
```bash
make build/client
./build/client
```

### Terminal 3 (benchmark)
```bash
make build/benchmark_flexql
./build/benchmark_flexql
```

---

## 9) Utility/Helper Scripts in Repository

These files exist in the project root:

```bash
./patch_cache.sh
./patch_server.sh
./test_disable_cache.sh
```

Notes:
- `patch_cache.sh` and `patch_server.sh` are currently empty stubs.
- `test_disable_cache.sh` modifies cache logic in `src/server/server.cpp`, rebuilds server, and runs benchmark.

Run helper script example:
```bash
bash test_disable_cache.sh
```

---

## 10) Common Troubleshooting Commands

### Stop any running server process
```bash
pkill -f "./build/server" || true
```

### Check server process is running
```bash
pgrep -af "./build/server"
```

### Check if port 9000 is occupied
```bash
ss -ltnp | grep :9000 || true
```

---

## 11) SQL Commands Supported by Server (via client)

```sql
CREATE TABLE ...
INSERT INTO ... VALUES (...)
SELECT ... FROM ... [WHERE ...] [LIMIT ...]
SELECT ... FROM A INNER JOIN B ON ...
DROP TABLE ...
```

(DELETE/UPDATE parser/test scaffolding exists, but validate your exact runtime behavior with current server before relying on those in production flows.)

---

## 12) Example Commands You Can Use in This Project

### Example A: Start from clean state and run server/client

Terminal 1:
```bash
make clean
make build/server
./build/server
```

Terminal 2:
```bash
make build/client
./build/client
```

### Example B: Basic table lifecycle (in client prompt)

```sql
DROP TABLE IF EXISTS USERS;
CREATE TABLE USERS(ID DECIMAL, NAME VARCHAR(64), EMAIL VARCHAR(64), BALANCE DECIMAL, EXPIRES_AT DATETIME);
INSERT INTO USERS VALUES(1, 'ALICE', 'alice@mail.com', 1200, '2026-12-31 23:59:59');
INSERT INTO USERS VALUES(2, 'BOB', 'bob@mail.com', 800, 1893456000);
SELECT * FROM USERS;
SELECT NAME, BALANCE FROM USERS WHERE BALANCE > 900;
SELECT COUNT(*) FROM USERS;
DROP TABLE USERS;
```

### Example C: JOIN query example (in client prompt)

```sql
DROP TABLE IF EXISTS U;
DROP TABLE IF EXISTS O;
CREATE TABLE U(ID DECIMAL, NAME VARCHAR(64), EXPIRES_AT DECIMAL);
CREATE TABLE O(ORDER_ID DECIMAL, USER_ID DECIMAL, AMOUNT DECIMAL, EXPIRES_AT DECIMAL);
INSERT INTO U VALUES(1, 'ALICE', 1893456000),(2, 'BOB', 1893456000);
INSERT INTO O VALUES(101, 1, 50, 1893456000),(102, 1, 150, 1893456000),(103, 2, 70, 1893456000);
SELECT U.NAME, O.AMOUNT FROM U INNER JOIN O ON U.ID = O.USER_ID WHERE O.AMOUNT > 60;
```

### Example D: DATETIME parsing + display example

```sql
DROP TABLE IF EXISTS DT_TEST;
CREATE TABLE DT_TEST(ID DECIMAL, CREATED_AT DATETIME);
INSERT INTO DT_TEST VALUES(1, '2026-04-07 10:30:00');
INSERT INTO DT_TEST VALUES(2, 1893456000);
SELECT * FROM DT_TEST;
SELECT * FROM DT_TEST WHERE CREATED_AT = '2026-04-07 10:30:00';
```

### Example E: Run all tests and one focused test

```bash
make test
make build/test_executor && ./build/test_executor
```

### Example F: Run benchmark end-to-end

Terminal 1:
```bash
pkill -f "./build/server" || true
make build/server
./build/server
```

Terminal 2:
```bash
make benchmark
```

### Example G: Build-only quick commands

```bash
make build/server build/client build/benchmark_flexql
```
