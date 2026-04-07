# FlexQL

A high-performance, in-memory SQL database engine written in C++11. FlexQL is designed for throughput-first workloads, supporting bulk inserts of 1M+ rows, low-latency queries, and persistent storage via snapshots and write-ahead logging (WAL).

## Features

- **High-throughput inserts** -- optimized batch insert path handles 1M+ rows with minimal overhead
- **SQL subset support** -- CREATE TABLE, INSERT, SELECT, DELETE, UPDATE, JOIN, DROP TABLE
- **Client-server architecture** -- TCP server on port 9000 with a C-compatible client API
- **Persistent storage** -- binary snapshots (`.tbl`) and WAL (`.wal`) for crash recovery
- **In-memory execution** -- all active tables reside in memory for low-latency reads
- **Hash join execution** -- efficient join implementation with build/probe phases
- **Concurrent access** -- thread-per-client model with per-table locking

## Project Structure

```
newFlexQL2/
├── include/                   # Public headers
│   ├── flexql.h               # C API (flexql_open, flexql_exec, flexql_close)
│   ├── executor/executor.hpp  # Query AST structures and executor API
│   ├── index/index.hpp        # Index interface (int, decimal, string)
│   ├── parser/parser.hpp      # SQL parser functions
│   ├── planner/planner.hpp    # Query planner (stub)
│   └── storage/storage.hpp    # Storage engine types (Table, Column, Value)
├── src/
│   ├── client/
│   │   ├── api.cpp            # C API implementation (TCP client)
│   │   └── client.cpp         # Interactive REPL client
│   ├── executor/
│   │   ├── executor.cpp       # Query execution (SELECT, INSERT, JOIN, etc.)
│   │   └── resolver.cpp       # Column resolver
│   ├── index/index.cpp        # Map-based index (int, decimal, string)
│   ├── parser/parser.cpp      # Hand-written recursive descent SQL parser
│   ├── planner/planner.cpp    # Query planner (stub)
│   ├── server/server.cpp      # TCP server, query dispatch, LRU cache
│   ├── storage/storage.cpp    # Row-oriented storage engine
│   └── main.cpp               # Interactive query runner entry point
├── tests/
│   ├── benchmark_flexql.cpp   # Functional + performance benchmarks
│   ├── benchmark_insert.cpp   # Insert-specific benchmarks
│   ├── test_storage.cpp       # Storage engine tests
│   ├── test_index.cpp         # Index tests
│   ├── test_executor.cpp      # Executor tests
│   ├── test_parser.cpp        # Parser tests
│   └── ...                    # Additional test files
├── tables/                    # Persisted table data (.tbl, .schema, .wal)
└── Makefile                   # Build system
```

## Building

**Requirements:** g++ with C++11 support, GNU Make, POSIX system (Linux/macOS)

```bash
# Build everything (tests + benchmarks)
make all

# Build and run the server
make server

# Build and run the interactive client
make client

# Build and run all tests
make test

# Build and run benchmarks
make benchmark
```

Individual test targets are also available: `make test_storage`, `make test_parser`, etc.

```bash
# Clean build artifacts and table data
make clean
```

## Usage

### Starting the Server

```bash
make server
# Server listens on port 9000
```

### Using the C API

```c
#include "flexql.h"

// Connect
flexql *db = flexql_open("127.0.0.1", 9000, "mydb");

// Execute SQL
char *errmsg = NULL;
flexql_exec(db, "CREATE TABLE users (id INT, name STRING, score DECIMAL);",
            NULL, NULL, &errmsg);

flexql_exec(db, "INSERT INTO users VALUES (1, 'Alice', 95.5), (2, 'Bob', 87.0);",
            NULL, NULL, &errmsg);

// Query with callback
int callback(void *arg, int ncols, char **values, char **colnames) {
    for (int i = 0; i < ncols; i++)
        printf("%s=%s ", colnames[i], values[i]);
    printf("\n");
    return 0;
}
flexql_exec(db, "SELECT * FROM users WHERE score > 90;",
            callback, NULL, &errmsg);

// Cleanup
flexql_close(db);
```

### Supported SQL

```sql
CREATE TABLE tablename (col1 INT, col2 DECIMAL, col3 STRING);
INSERT INTO tablename VALUES (1, 3.14, 'hello'), (2, 2.71, 'world');
SELECT * FROM tablename WHERE col1 > 0 LIMIT 100;
SELECT col1, col2 FROM tablename;
SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id WHERE t1.col > 5;
DELETE FROM tablename;
DROP TABLE tablename;
```

**Supported types:** `INT`, `DECIMAL`, `STRING` / `VARCHAR` / `TEXT`

## Supported Data Types

| Type | Storage | Size |
|------|---------|------|
| `INT` | 64-bit signed integer | 8 bytes |
| `DECIMAL` | 64-bit double | 8 bytes |
| `STRING` / `VARCHAR` / `TEXT` | Variable-length bytes in string pool | 4 + length bytes |

## Running Tests

```bash
# All tests
make test

# Individual component tests
make test_storage
make test_index
make test_executor
make test_parser
make test_delete
make test_update
make test_persistence

# Benchmarks (requires server running for benchmark_flexql)
make benchmark
make benchmark_insert
```

## Configuration

All configuration is currently compile-time:

| Parameter | Value | Location |
|-----------|-------|----------|
| Server port | 9000 | `server.cpp` |
| Table directory | `tables/` | `server.cpp` |
| Query cache size | 1000 entries | `server.cpp` |
| Client read buffer | 256 KB | `server.cpp` |
| Write buffer threshold | 100 KB | `storage.cpp` |
| WAL flush threshold | 4 MB | `server.cpp` |

## Dependencies

None beyond standard library. FlexQL is a pure C++11 implementation using only:
- C++ Standard Library (STL)
- POSIX sockets (`sys/socket.h`, `netinet/in.h`)
- `libm` (math library, linked via `-lm`)
