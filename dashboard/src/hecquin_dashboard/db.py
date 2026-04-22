"""SQLite access layer shared by all repositories.

Two connection flavours:

* **Readers** — opened read-only via the URI form ``file:...?mode=ro`` so the
  dashboard can never corrupt data written by the C++ side. Safe for concurrent
  use because LearningStore opens with ``PRAGMA journal_mode=WAL``.
* **Writers** — a single dedicated connection used by the request-log
  middleware to insert rows into the ``request_logs`` table. The table schema
  itself is created by the C++ migration; we only ever ``INSERT`` here.

Rows are returned as ``sqlite3.Row`` so repositories can index by column name.
"""

from __future__ import annotations

import sqlite3
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from threading import Lock


def _ro_uri(path: Path) -> str:
    # `nolock=1` would break with WAL writers; keep default locking so SQLite
    # co-operates with the C++ writer.
    return f"file:{path}?mode=ro"


def open_reader(path: Path) -> sqlite3.Connection:
    """Open a fresh read-only connection. Caller is responsible for closing."""
    conn = sqlite3.connect(
        _ro_uri(path),
        uri=True,
        detect_types=sqlite3.PARSE_DECLTYPES,
        check_same_thread=False,
    )
    conn.row_factory = sqlite3.Row
    # Match the writer's WAL expectations; these PRAGMAs are harmless on RO.
    conn.execute("PRAGMA query_only = 1;")
    return conn


class WriterConnection:
    """Single-writer connection used by the request_logs middleware.

    SQLite serialises writers per-database, so we hold exactly one connection
    behind a lock to avoid ``database is locked`` errors when many requests
    arrive simultaneously. The connection is created lazily on first use.
    """

    def __init__(self, path: Path) -> None:
        self._path = path
        self._lock = Lock()
        self._conn: sqlite3.Connection | None = None

    def _ensure(self) -> sqlite3.Connection:
        if self._conn is None:
            self._conn = sqlite3.connect(
                str(self._path),
                isolation_level=None,  # autocommit; each INSERT is its own tx
                check_same_thread=False,
                timeout=5.0,
            )
            self._conn.execute("PRAGMA journal_mode=WAL;")
            self._conn.execute("PRAGMA synchronous=NORMAL;")
        return self._conn

    def execute(self, sql: str, params: tuple = ()) -> None:
        with self._lock:
            conn = self._ensure()
            conn.execute(sql, params)

    def close(self) -> None:
        with self._lock:
            if self._conn is not None:
                self._conn.close()
                self._conn = None


@contextmanager
def read_connection(path: Path) -> Iterator[sqlite3.Connection]:
    """Context-managed read-only connection for one request."""
    conn = open_reader(path)
    try:
        yield conn
    finally:
        conn.close()
