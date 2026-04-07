# FlexQL Design Document

## 1. Overview

FlexQL is a high-throughput, in-memory SQL database engine with a client-server architecture over TCP. It is designed for workloads that prioritize insert throughput and query latency over strict ACID durability guarantees.

**Design philosophy:** Throughput over durability. Simplicity over features. Memory residency over bounded memory.

### Goals
- Sustain 1M+ row bulk inserts with minimal overhead
- Sub-millisecond query latency for in-memory data
- Network-accessible via a simple C API over TCP
- Practical crash recovery via snapshots and WAL
- Minimal external dependencies (pure C++11, POSIX sockets)

### Non-Goals
- Full ACID compliance (no per-transaction fsync)
- Distributed or replicated operation
- Complex query optimization (no cost-based planner)
- Bounded memory usage or eviction policies

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Client Application                    │
│               (C API: flexql.h / api.cpp)               │
└──────────────────────────┬──────────────────────────────┘
                           │ TCP (port 9000)
                           │ Text protocol: SQL → ROW/END
┌──────────────────────────▼──────────────────────────────┐
│                       TCP Server                         │
│                    (server/server.cpp)                    │
│  ┌──────────┐  ┌──────────┐  ┌────────────────────┐     │
│  │ Accept   │  │ Per-     │  │ LRU Query Cache    │     │
│  │ Loop     │→ │ Client   │  │ (1000 entries)     │     │
│  │          │  │ Threads  │  └────────────────────┘     │
│  └──────────┘  └────┬─────┘                              │
└──────────────────────┼──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│                    SQL Parser                             │
│                  (parser/parser.cpp)                      │
│                                                          │
│  Hand-written recursive descent parser                   │
│  Fast path: direct pointer scanning for INSERT VALUES    │
└──────────────────────┬──────────────────────────────────┘
                       │ AST (SelectQuery, InsertQuery, etc.)
┌──────────────────────▼──────────────────────────────────┐
│                   Query Executor                         │
│                (executor/executor.cpp)                    │
│                                                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ SELECT   │ │ INSERT   │ │  JOIN    │ │ DELETE/  │   │
│  │ (scan/   │ │ (batch   │ │ (hash   │ │ UPDATE   │   │
│  │  index)  │ │  encode) │ │  join)  │ │ (basic)  │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
┌──────────────┐ ┌──────────┐ ┌──────────────┐
│   Storage    │ │  Index   │ │ Persistence  │
│   Engine     │ │  Layer   │ │ (WAL +       │
│ (row store,  │ │ (map/    │ │  snapshots)  │
│  string pool)│ │  hash)   │ │              │
└──────────────┘ └──────────┘ └──────────────┘
```

### Component Responsibilities

| Component | File(s) | Responsibility |
|-----------|---------|----------------|
| **C API** | `flexql.h`, `client/api.cpp` | TCP connection management, SQL send/receive, callback-based row delivery |
| **TCP Server** | `server/server.cpp` | Connection accept, per-client threading, query dispatch, result streaming, LRU cache |
| **Parser** | `parser/parser.cpp` | SQL tokenization and AST construction for all supported statement types |
| **Executor** | `executor/executor.cpp` | Query execution: table scans, index lookups, hash joins, row encoding/decoding |
| **Storage** | `storage/storage.cpp` | In-memory row store, binary row encoding, batch insert, snapshot I/O |
| **Index** | `index/index.cpp` | Key-to-offset mapping via `std::map` (int/decimal) and `std::unordered_map` (string) |

---

## 3. Storage Engine

### 3.1 Row Format

Rows are stored as contiguous binary blobs in an append-only write buffer:

```
┌──────────────┬────────────────────┬──────────────────────────────┐
│ row_size (4B)│ null_bitmap        │ column data                  │
│              │ ⌈cols/8⌉ bytes     │ (8B per INT/DECIMAL,         │
│              │                    │  4B len + bytes per STRING)  │
└──────────────┴────────────────────┴──────────────────────────────┘
```

- **INT / DECIMAL**: Fixed 8-byte encoding (int64_t / double via memcpy)
- **STRING**: 4-byte length prefix followed by raw bytes, stored in a separate string pool
- **Null bitmap**: One bit per column, packed into `(col_count + 7) / 8` bytes

### 3.2 Table Structure

```cpp
struct Table {
    std::string name;
    std::vector<Column> columns;        // Schema definition
    std::vector<uint8_t> write_buffer;  // Append-only row data
    std::vector<std::string> string_pool;
    std::mutex table_mutex;             // Per-table lock
    uint64_t row_count;
};
```

Tables are entirely memory-resident. The write buffer grows via append-only semantics -- rows are never updated in place.

### 3.3 On-Disk Format

**Snapshot (`.tbl`):** Binary file containing raw row data. Written in full on checkpoint.

**Schema (`.schema`):** Text sidecar file for human-readable schema:
```
id INT
name STRING
score DECIMAL
```

**WAL (`.wal`):** Append-only framed log:
```
[magic][version][op][payload_size][checksum][payload]
```
- Frames are buffered in memory and flushed at 4 MB threshold
- Checksum is XOR/rotate over payload + metadata
- Large payloads (>= 2 MB) bypass the buffer and write directly

### 3.4 Persistence and Recovery

**Checkpoint sequence:**
1. Flush in-memory WAL buffer to `.wal` file
2. Write full snapshot to `.tbl`
3. Write schema to `.schema`
4. Truncate WAL

**Crash recovery:**
1. Load snapshot from `.tbl`
2. Replay WAL frames in order
3. Stop at first invalid frame (truncation or checksum mismatch)

**Durability trade-off:** No `fsync` barriers are used. Recent writes may be lost on OS crash or power failure. This is an intentional choice favoring throughput.

---

## 4. Query Execution

### 4.1 Parser

Hand-written recursive descent parser supporting:
- `CREATE TABLE name (col type, ...)`
- `INSERT INTO name VALUES (...), (...), ...`
- `SELECT [cols|*] FROM name [WHERE cond] [LIMIT n]`
- `SELECT ... FROM t1 INNER JOIN t2 ON t1.c = t2.c [WHERE ...] [LIMIT n]`
- `DELETE FROM name`
- `DROP TABLE name`

**Optimization:** The INSERT path uses direct pointer scanning over the VALUES clause, avoiding tokenization entirely. The parser estimates row count by counting `(` characters and pre-allocates accordingly.

### 4.2 SELECT Execution

```
SELECT * FROM users WHERE score > 90 LIMIT 10;
```

1. Acquire per-table lock (shared)
2. Flush any pending write buffer
3. Sequential scan over all rows in write buffer
4. For each row: decode binary -> DecodedRow, evaluate WHERE predicate
5. If match: format result, decrement LIMIT counter
6. Stop on LIMIT exhaustion or end of table

Index fast path exists for `WHERE pk = literal` but is not used in the general case.

### 4.3 INSERT Execution

```
INSERT INTO users VALUES (1, 'Alice', 95.5), (2, 'Bob', 87.0);
```

1. Parse table name and VALUES clause (fast pointer scanning)
2. Acquire per-table lock (exclusive)
3. Pre-allocate index capacity and WAL buffer
4. For each tuple:
   - Parse field values from raw SQL text
   - Encode row into binary format
   - Append to write buffer
   - Update primary index
5. Append WAL frame (buffered)

**Batch optimization:** The benchmark client sends 5000 rows per INSERT statement to minimize round trips and amortize lock acquisition.

### 4.4 JOIN Execution (Hash Join)

```
SELECT * FROM orders INNER JOIN users ON orders.user_id = users.id;
```

1. **Build phase:** Scan right table (users), hash join column values to row offset lists
2. **Probe phase:** Scan left table (orders), look up each join key in hash index
3. For each match: decode both rows, evaluate WHERE clause, emit combined row
4. Respect LIMIT by decrementing counter

Deadlock prevention: Both table locks acquired simultaneously using `std::lock()`.

---

## 5. Indexing

### 5.1 Index Types

| Type | Structure | Use Case |
|------|-----------|----------|
| `IntIndex` | `std::map<int64_t, uint64_t>` | Integer primary keys |
| `DecimalIndex` | `std::map<double, uint64_t>` | Decimal primary keys |
| `StringIndex` | `std::unordered_map<string, uint64_t>` | String primary keys |

Indexes map key values to row offsets in the write buffer.

### 5.2 Hybrid Primary Index (Deep Dive)

The primary index on the first numeric column uses a hybrid strategy:
- **Sequential mode:** Compact vector of (key, offset) pairs for monotonically increasing keys (common in benchmarks and auto-increment IDs)
- **Hash mode:** Automatic fallback to open-addressed hash table with linear probing when the monotonic pattern breaks

This design optimizes for the common case (sequential inserts) while remaining correct for general workloads.

---

## 6. Networking

### 6.1 Protocol

Text-based protocol over TCP:

**Request:** Raw SQL terminated by `;` and newline
```
SELECT * FROM users;
```

**Response:** Pipe-delimited rows followed by `END`:
```
id|name|score
1|Alice|95.5
2|Bob|87.0
END
```

**Error:**
```
ERROR: table not found
END
```

### 6.2 Server Architecture

- **Accept loop:** Main thread accepts connections, spawns one detached `std::thread` per client
- **Per-client handler:** Reads bytes into a pending buffer, extracts semicolon-delimited statements, dispatches to executor
- **Response batching:** All result rows are accumulated into a single string, sent with one `send()` call to minimize syscalls
- **Query cache:** LRU cache (1000 entries) keyed by SQL text, stores serialized result strings

### 6.3 Buffer Sizes

| Buffer | Size | Purpose |
|--------|------|---------|
| Server read buffer | 256 KB per client | Absorb large INSERT statements |
| Pending buffer reserve | 10 MB | Avoid reallocation during large queries |
| Client read buffer | 4 MB | Read multi-row results efficiently |

---

## 7. Concurrency

### 7.1 Threading Model

Thread-per-client with a global singleton executor:

```
Main thread          Client thread 1      Client thread 2
    │                     │                     │
    │ accept()            │                     │
    │────────►spawn       │                     │
    │                     │ handle_client()     │
    │ accept()            │                     │
    │────────────────────────────►spawn         │
    │                     │                     │ handle_client()
```

### 7.2 Lock Hierarchy

| Lock | Type | Protects |
|------|------|----------|
| `g_tables_mutex` | `std::mutex` | Global table registry (create/drop/lookup) |
| `Table::table_mutex` | `std::mutex` | Per-table row data and indexes |
| `wal_mutex` | `std::mutex` | WAL buffer map |

**Lock rules:**
- SELECT: acquires table lock (simulates shared access)
- INSERT / DELETE: acquires table lock (exclusive)
- JOIN: acquires both table locks via `std::lock()` to prevent deadlock
- Table registry lock is always acquired before table data locks

### 7.3 Contention Characteristics

- Writers to the same table are serialized (exclusive lock)
- Readers can run concurrently (shared lock simulation)
- Fast insert path minimizes critical section duration
- No connection limit -- thread count can grow unbounded

---

## 8. Performance Optimizations

### 8.1 Insert Hot Path

The insert path is the most optimized code path, with these techniques layered:

1. **Parser bypass:** INSERT detected by string prefix, avoiding full AST parse
2. **Direct pointer scanning:** VALUES clause parsed character-by-character, no tokenizer objects
3. **Pre-allocation:** Index capacity and WAL buffer reserved based on estimated row count
4. **Batch encoding:** Rows encoded in a tight loop with minimal allocations
5. **Buffered WAL:** Frames accumulated in memory, flushed at 4 MB threshold
6. **Buffered I/O:** Snapshot files use 1 MB stdio buffers

### 8.2 Query Path

- Pre-allocated formatting buffers for numeric-to-string conversion
- Early termination on LIMIT
- Single-pass scan with inline predicate evaluation
- Response payload batching (one `send()` call per query)

### 8.3 Memory Layout

- Append-only write buffer avoids fragmentation
- Separate string pool prevents row data from bloating on variable-length fields
- No per-row heap allocation in the hot path

---

## 9. Type System

```
TYPE_INT     = 0    →  int64_t    (8 bytes)
TYPE_DECIMAL = 1    →  double     (8 bytes, bit-preserving memcpy encoding)
TYPE_STRING  = 2    →  variable   (4-byte length + raw bytes in string pool)
```

**Limits:**
- Max columns per table: 100
- Max column name length: 64 characters
- Max table name length: 64 characters

---

## 10. Expiration

FlexQL supports optional row expiration via a convention-based `EXPIRES_AT` column:

- Detected by column name (uppercase match)
- Stored as a numeric epoch value
- Expired rows are filtered at read time (lazy cleanup)
- No background garbage collection
- Expired rows continue to occupy memory and disk until checkpoint with exclusion

---

## 11. Known Limitations

| Limitation | Rationale |
|------------|-----------|
| No ACID guarantees | Throughput-first design; no fsync |
| Table-level locking | Simplicity; sufficient for target workloads |
| Thread-per-client | Simple to implement; unbounded under high connections |
| No query optimizer | Table scans are fast enough for in-memory data |
| Basic DELETE/UPDATE | Stubs; not fully implemented |
| No compression | Simplicity; raw binary rows |
| No replication | Single-node only |
| Unbounded memory | No eviction policy; tables grow with data |
| No authentication | Trusts all TCP connections |

---

## 12. Future Directions

Potential improvements (not currently planned):

- **Connection pooling / epoll:** Replace thread-per-client with event-driven I/O
- **Row-level locking or MVCC:** Enable concurrent writes to the same table
- **Cost-based query planner:** Use index statistics to choose scan vs. index lookup
- **Background compaction:** Reclaim space from expired/deleted rows
- **Columnar storage option:** For analytical query patterns
- **fsync modes:** Configurable durability levels (none / per-checkpoint / per-transaction)
- **Prepared statements:** Avoid re-parsing repeated queries
- **Authentication and TLS:** Secure client connections



## 13) Other Details

### 13.1 How the data is stored

FlexQL stores table data in binary row format inside `tables/<TABLE>.db`, with schema metadata in `tables/<TABLE>.schema`.

Each row is encoded as:
1. Row size (4 bytes)
2. NULL bitmap (ceil(columns/8) bytes)
3. Column payload in schema order:
   - INT: 8 bytes
   - DECIMAL: 8 bytes
   - STRING: 4-byte length + bytes

Write path details:
- Rows are first appended to an in-memory `write_buffer`.
- Buffer flushes to file when threshold is reached (`BATCH_SIZE_BYTES`, currently 100KB) or before scans/select reads.
- Table metadata keeps file size for offset-based reads/scans.

Type behavior:
- Logical DATETIME columns are internally represented as numeric (DECIMAL) epoch seconds.
- Datetime strings are parsed server-side and converted to epoch seconds before storage.

---

### 13.2 Indexing method

Index module supports key -> row offset mapping:
- Integer index: `std::map<int64_t, uint64_t>`
- Decimal index: `std::map<double, uint64_t>`
- String index: `std::unordered_map<std::string, uint64_t>`

Current practical behavior:
- Index APIs are implemented and usable.
- Main server select path currently defaults to scan execution unless an index is explicitly attached in execution planning.

---

### 13.3 Caching strategy

FlexQL uses an in-memory LRU query-result cache in the server:
- Capacity: 1000 entries
- Key: full SQL text
- Value: serialized SELECT response

Cache lifecycle:
- SELECT checks cache first; cache miss executes query and stores result.
- INSERT/CREATE TABLE/DROP TABLE clear cache to maintain correctness.

---

### 13.4 Handling of expiration timestamps

There is no automatic TTL eviction worker.

Expiration is handled at query time:
- Application stores expiration values (typically epoch seconds) in columns like `EXPIRES_AT`.
- Queries filter active/expired rows using predicates, for example:
  - `SELECT * FROM BIG_USERS WHERE EXPIRES_AT > 1893456000;`

So expiration semantics are explicit and query-driven.

---

### 13.5 Multithreading design

Server concurrency model:
- One accept loop in main thread.
- One detached worker thread per client connection.

Synchronization:
- Global table registry lock: `g_tables_mutex`
- Per-table lock: `table->table_mutex`
- JOIN acquires two table locks using `std::lock` for deadlock-safe lock ordering.

Properties:
- Good simplicity and straightforward correctness.
- Writes on the same table are serialized by per-table mutex.

---

### 13.6 Other design decisions

- Protocol is text SQL over TCP with `END` terminator in responses.
- Parser is hand-written for low overhead and direct control of supported SQL subset.
- SELECT formatting includes DATETIME rendering as `YYYY-MM-DD HH:MM:SS` for DATETIME logical columns.
- Query execution favors predictable scan-based behavior and simplicity over complex optimizer logic.

---

## 14) Compilation and execution instructions

### Build everything
make all

### Run server
make server

### Run client
make client

### Run benchmark (requires server running)
make benchmark

### Run focused executor test
make build/test_executor && ./build/test_executor

### Clean build/data
make clean

---

## 15) Performance results for large datasets

Benchmark source: `tests/benchmark_flexql.cpp`
- Target insert rows: 10,000,000
- Batch size in benchmark: 10,000 rows per INSERT statement

Latest measured run on this workspace:
- Rows inserted: 10,000,000
- Elapsed: 6794 ms
- Throughput: 1,471,886 rows/sec
- Post-benchmark functional checks: 21/21 passed

Interpretation:
- Current pipeline sustains >1.4M rows/sec for large batched SQL inserts.
- Throughput is mostly limited by SQL text creation/parsing + binary row encoding + buffered file writes.
- Read/access speed remains strong for scan-based analytics and indexed point lookups when index is available.

