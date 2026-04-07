# FlexQL — Performance Results for This Project

## Test Environment

| Parameter           | Value                                     |
| ------------------- | ----------------------------------------- |
| OS                  | Linux (x86-64)                            |
| Build flags         | `-O3` (from Makefile)                     |
| Server binary       | `./build/server`                          |
| Benchmark binary    | `./build/benchmark_flexql`                |
| Query cache         | LRU, 1000 entries (`g_query_cache(1000)`) |
| Storage persistence | `.db` + `.schema` files under `tables/`   |

---

## 1) Insertion Benchmark — `make benchmark`

Benchmark source: `tests/benchmark_flexql.cpp`

### Default Large Dataset Run (10,000,000 rows)

Recent run output from this workspace:

```text
Starting insertion benchmark for 10000000 rows...
...
[PASS] INSERT benchmark complete
Rows inserted: 10,000,000
Elapsed: 6230 ms
Throughput: 1,605,136 rows/sec
```

### Throughput Summary

| Rows Inserted | Elapsed (ms) | Throughput (rows/sec) |
| ------------- | -----------: | --------------------: |
| 10,000,000    |         6230 |             1,605,136 |

Notes:

- Inserts are generated as large multi-row SQL statements (batch size 10,000 in benchmark generator).
- Throughput includes client SQL generation + network transfer + server parse + row encoding + file append.

---

## 2) Integrated Unit Checks in Benchmark

`make benchmark` also runs the embedded data-level checks from `tests/benchmark_flexql.cpp` after insertion.

Recent run summary:

```text
Unit Test Summary: 21/21 passed, 0 failed.
```

Covered checks include:

- Basic `SELECT *` correctness
- Predicate filtering correctness
- Empty result behavior
- Join behavior (`INNER JOIN` validation scenario)
- Invalid SQL and missing table error behavior

---

## 3) Query Cache Behavior and Impact

Server implements an LRU query-result cache in `src/server/server.cpp`:

- Cache capacity: 1000 entries
- Key: full SQL query string
- Value: serialized response text

Behavior:

- Cache hit: server serves response without re-running query execution.
- Invalidation: cache is cleared on `INSERT`, `CREATE TABLE`, and `DROP TABLE`.

Practical implication:

- Repeated identical `SELECT` queries benefit most.
- Write-heavy workloads invalidate cache frequently, reducing cache-hit contribution.

---

## 4) Large Dataset Characteristics

At 10M rows, observed performance profile is dominated by:

1. SQL text construction/transmission for large `INSERT ... VALUES (...)` payloads
2. Parsing and type coercion on server
3. Binary row encoding and batched write-buffer flushes

Current system characteristics:

- Strong bulk-ingest throughput for a simple SQL/TCP architecture
- Stable post-ingest query correctness based on benchmark checks
- No WAL/checkpoint replay path measured in this project benchmark flow

---

## 5) SELECT Query Time Benchmark (1,000,000-row Access)

Benchmark script:

```bash
python3 scripts/select_query_benchmark.py --rows 1000000 --runs 3 --full-scan-runs 1
```

Measured results:

| Query                    | Runs | Rows(returned,last run) | Min(ms) | Avg(ms) | P50(ms) | Max(ms) |
| ------------------------ | ---: | ----------------------: | ------: | ------: | ------: | ------: |
| FullScan(1M)             |    1 |               1,000,000 | 2619.84 | 2619.84 | 2619.84 | 2619.84 |
| Limit10                  |    3 |                      10 |   41.09 |   41.49 |   41.09 |   42.30 |
| Limit1000                |    3 |                   1,000 |   41.27 |   41.56 |   41.49 |   41.92 |
| WhereEq(ID=500000)       |    3 |                       1 |   40.61 |   40.76 |   40.73 |   40.93 |
| WhereRange(BALANCE>5000) |    3 |                 599,900 |  899.77 |  906.60 |  900.28 |  919.75 |
| CountAll                 |    3 |                       1 |   40.64 |   40.84 |   40.74 |   41.13 |

### Purpose of each result column

- `Query`: The SQL pattern being benchmarked.
- `Runs`: Number of timed executions used for statistics.
- `Rows(returned,last run)`: Number of rows returned by the last timed execution of that query.
- `Min(ms)`: Best-case latency across all timed runs.
- `Avg(ms)`: Average latency across all timed runs.
- `P50(ms)`: Median latency (50th percentile), robust against outliers.
- `Max(ms)`: Worst-case latency across all timed runs.

### Recorded time for selecting 1 million rows

- `SELECT * FROM SELECT_BENCH;` on a 1,000,000-row table took **2619.84 ms** in this run.

---

## 6) Summary

| Metric                             | Result                                                |
| ---------------------------------- | ----------------------------------------------------- |
| Large insert throughput (10M rows) | **~1.60M rows/sec**                                   |
| Benchmark elapsed (10M rows)       | **6230 ms**                                           |
| Full table select (1M rows)        | **2619.84 ms**                                        |
| Embedded benchmark checks          | **21 / 21 passed**                                    |
| Query cache                        | ✅ LRU, 1000 entries                                  |
| Expiration handling                | ✅ Query-time filtering via numeric timestamp columns |

---

## 7) Reproduction Commands

### Build

```bash
make build/server build/benchmark_flexql
```

### Run server

```bash
./build/server
```

### Run benchmark (separate terminal)

```bash
make benchmark
```

### Optional: focused executor test

```bash
make build/test_executor && ./build/test_executor
```

Target insert rows: 10000000

[PASS] CREATE TABLE BIG_USERS (0 ms)

Starting insertion benchmark for 10000000 rows...
Progress: 1000000/10000000
Progress: 2000000/10000000
Progress: 3000000/10000000
Progress: 4000000/10000000
Progress: 5000000/10000000
Progress: 6000000/10000000
Progress: 7000000/10000000
Progress: 8000000/10000000
Progress: 9000000/10000000
Progress: 10000000/10000000
[PASS] INSERT benchmark complete
Rows inserted: 10000000
Elapsed: 6230 ms
Throughput: 1605136 rows/sec

[[...Running Unit Tests...]]

[PASS] CREATE TABLE TEST_USERS (0 ms)
[PASS] INSERT TEST_USERS ID=1 (41 ms)
[PASS] INSERT TEST_USERS ID=2 (0 ms)
[PASS] INSERT TEST_USERS ID=3 (40 ms)
[PASS] INSERT TEST_USERS ID=4 (0 ms)
[PASS] Basic SELECT \* validation
[PASS] Single-row value validation
[PASS] Filtered rows validation
[PASS] Empty result-set validation
[PASS] CREATE TABLE TEST_ORDERS (40 ms)
[PASS] INSERT TEST_ORDERS ORDER_ID=101 (0 ms)
[PASS] INSERT TEST_ORDERS ORDER_ID=102 (40 ms)
[PASS] INSERT TEST_ORDERS ORDER_ID=103 (0 ms)
[PASS] Join with no matches validation
[PASS] Invalid SQL should fail
[PASS] Missing table should fail

Unit Test Summary: 21/21 passed, 0 failed.
