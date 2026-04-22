"""Shared repository scaffolding.

All concrete repositories inherit `BaseRepo` so they share a connection
factory and helper methods. The indirection costs nothing at runtime and
keeps `services/` free of SQL.
"""

from __future__ import annotations

import sqlite3
from collections.abc import Callable, Iterable
from pathlib import Path
from typing import Any

from ..db import open_reader


class BaseRepo:
    """Repositories hold a ``Path`` and open fresh RO connections per call.

    FastAPI dependencies instantiate a repo per request, so we intentionally
    avoid caching a connection on ``self`` (which would otherwise need
    thread-local handling).
    """

    def __init__(self, db_path: Path) -> None:
        self._db_path = db_path

    def _connect(self) -> sqlite3.Connection:
        return open_reader(self._db_path)

    def _fetch_all(self, sql: str, params: Iterable[Any] = ()) -> list[sqlite3.Row]:
        with self._connect() as conn:
            cur = conn.execute(sql, tuple(params))
            return list(cur.fetchall())

    def _fetch_one(self, sql: str, params: Iterable[Any] = ()) -> sqlite3.Row | None:
        with self._connect() as conn:
            cur = conn.execute(sql, tuple(params))
            return cur.fetchone()

    def _scalar(self, sql: str, params: Iterable[Any] = (), default: Any = 0) -> Any:
        row = self._fetch_one(sql, params)
        if row is None:
            return default
        value = row[0]
        return default if value is None else value

    @staticmethod
    def _map(rows: list[sqlite3.Row], mapper: Callable[[sqlite3.Row], Any]) -> list:
        return [mapper(r) for r in rows]
