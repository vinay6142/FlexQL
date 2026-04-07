#!/usr/bin/env python3
import argparse
import socket
import statistics
import sys
import time
from typing import Dict, List, Tuple

END_MARKER = "\nEND\n"


def ensure_semicolon(sql: str) -> str:
    sql = sql.strip()
    if not sql.endswith(";"):
        sql += ";"
    return sql


def read_response(sock: socket.socket) -> str:
    chunks: List[str] = []
    response = ""

    while True:
        data = sock.recv(65536)
        if not data:
            raise RuntimeError("Connection closed by server")

        chunk = data.decode("utf-8", errors="replace")
        chunks.append(chunk)
        response = "".join(chunks)

        end_pos = response.find(END_MARKER)
        if end_pos != -1:
            return response[:end_pos]

        if response.endswith("END\n"):
            return response[:-4]


def exec_query(sock: socket.socket, sql: str) -> str:
    sql = ensure_semicolon(sql)
    sock.sendall((sql + "\n").encode("utf-8"))
    payload = read_response(sock)
    if payload.startswith("ERROR"):
        raise RuntimeError(payload.strip())
    return payload


def timed_query(sock: socket.socket, sql: str) -> Tuple[float, int]:
    start = time.perf_counter()
    payload = exec_query(sock, sql)
    elapsed_ms = (time.perf_counter() - start) * 1000.0

    lines = [line for line in payload.splitlines() if line.strip()]
    row_count = max(len(lines) - 1, 0)
    return elapsed_ms, row_count


def populate_table(sock: socket.socket, table: str, rows: int, batch_size: int) -> None:
    print(f"Preparing table {table} with {rows:,} rows (batch={batch_size})...")

    try:
        exec_query(sock, f"DROP TABLE {table};")
    except RuntimeError:
        pass

    exec_query(
        sock,
        f"CREATE TABLE {table}(ID DECIMAL, NAME VARCHAR(64), BALANCE DECIMAL, EXPIRES_AT DECIMAL);",
    )

    inserted = 0
    progress_step = max(rows // 10, 1)
    next_progress = progress_step

    while inserted < rows:
        current_batch = min(batch_size, rows - inserted)
        values = []
        for i in range(current_batch):
            row_id = inserted + i + 1
            values.append(
                f"({row_id}, 'user{row_id}', {1000 + (row_id % 10000)}, 1893456000)"
            )

        sql = f"INSERT INTO {table} VALUES " + ",".join(values) + ";"
        exec_query(sock, sql)
        inserted += current_batch

        if inserted >= next_progress or inserted == rows:
            print(f"  inserted: {inserted:,}/{rows:,}")
            next_progress += progress_step


def summarize(times: List[float]) -> Dict[str, float]:
    return {
        "min": min(times),
        "avg": sum(times) / len(times),
        "p50": statistics.median(times),
        "max": max(times),
    }


def benchmark(sock: socket.socket, table: str, full_scan_runs: int, normal_runs: int) -> None:
    query_plan = [
        ("FullScan(1M)", f"SELECT * FROM {table};", full_scan_runs),
        ("Limit10", f"SELECT * FROM {table} LIMIT 10;", normal_runs),
        ("Limit1000", f"SELECT * FROM {table} LIMIT 1000;", normal_runs),
        ("WhereEq(ID=500000)", f"SELECT * FROM {table} WHERE ID = 500000;", normal_runs),
        ("WhereRange(BALANCE>5000)", f"SELECT * FROM {table} WHERE BALANCE > 5000;", normal_runs),
        ("CountAll", f"SELECT COUNT(*) FROM {table};", normal_runs),
    ]

    print("\nRunning SELECT benchmarks...")
    print("| Query | Runs | Rows(returned,last run) | Min(ms) | Avg(ms) | P50(ms) | Max(ms) |")
    print("|---|---:|---:|---:|---:|---:|---:|")

    for label, sql, runs in query_plan:
        timings = []
        last_rows = 0

        exec_query(sock, sql)

        for _ in range(runs):
            elapsed_ms, rows = timed_query(sock, sql)
            timings.append(elapsed_ms)
            last_rows = rows

        stats = summarize(timings)
        print(
            f"| {label} | {runs} | {last_rows:,} | "
            f"{stats['min']:.2f} | {stats['avg']:.2f} | {stats['p50']:.2f} | {stats['max']:.2f} |"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark SELECT query latency for 1M-row access patterns (full scan, LIMIT, WHERE, COUNT)."
    )
    parser.add_argument("--host", default="127.0.0.1", help="FlexQL server host")
    parser.add_argument("--port", type=int, default=9000, help="FlexQL server port")
    parser.add_argument("--table", default="SELECT_BENCH", help="Benchmark table name")
    parser.add_argument("--rows", type=int, default=1_000_000, help="Rows to populate")
    parser.add_argument("--batch-size", type=int, default=5000, help="Rows per INSERT statement")
    parser.add_argument("--no-prepare", action="store_true", help="Skip DROP/CREATE/INSERT and only run SELECT benchmarks")
    parser.add_argument("--full-scan-runs", type=int, default=1, help="Number of timed runs for full 1M-row scan")
    parser.add_argument("--runs", type=int, default=3, help="Number of timed runs for LIMIT/WHERE/COUNT queries")
    args = parser.parse_args()

    try:
        with socket.create_connection((args.host, args.port), timeout=10) as sock:
            sock.settimeout(120)

            if not args.no_prepare:
                populate_table(sock, args.table, args.rows, args.batch_size)

            benchmark(sock, args.table, args.full_scan_runs, args.runs)

        print("\nDone.")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
