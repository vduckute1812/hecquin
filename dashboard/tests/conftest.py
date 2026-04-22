"""Shared pytest fixtures.

Every fixture that needs a SQLite file creates a **fresh** one seeded with
the same DDL the C++ `LearningStore::run_migrations_` writes, then inserts a
deterministic set of rows. We replicate the DDL here (rather than invoking
the C++ code) so tests stay hermetic — no CMake, no SQLite-vec required.
"""

from __future__ import annotations

import sqlite3
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from hecquin_dashboard.config import Settings, get_settings
from hecquin_dashboard.main import create_app

# ---------- schema (mirrors sound/src/learning/LearningStore.cpp, v2) -------
_DDL: tuple[str, ...] = (
    "CREATE TABLE kv_metadata (key TEXT PRIMARY KEY, value TEXT);",
    "CREATE TABLE sessions ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " mode TEXT NOT NULL, started_at INTEGER NOT NULL, ended_at INTEGER);",
    "CREATE TABLE interactions ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " session_id INTEGER, user_text TEXT NOT NULL,"
    " corrected_text TEXT, grammar_notes TEXT, created_at INTEGER NOT NULL);",
    "CREATE TABLE vocab_progress ("
    " word TEXT PRIMARY KEY, first_seen_at INTEGER NOT NULL,"
    " last_seen_at INTEGER NOT NULL,"
    " seen_count INTEGER NOT NULL DEFAULT 1,"
    " mastery INTEGER NOT NULL DEFAULT 0);",
    "CREATE TABLE pronunciation_attempts ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " session_id INTEGER, reference TEXT NOT NULL, transcript TEXT,"
    " pron_overall REAL, intonation_overall REAL,"
    " per_phoneme_json TEXT, created_at INTEGER NOT NULL);",
    "CREATE TABLE phoneme_mastery ("
    " ipa TEXT PRIMARY KEY,"
    " attempts INTEGER NOT NULL DEFAULT 0,"
    " avg_score REAL NOT NULL DEFAULT 0.0,"
    " last_seen_at INTEGER NOT NULL);",
    "CREATE TABLE api_calls ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " ts INTEGER NOT NULL, provider TEXT NOT NULL, endpoint TEXT NOT NULL,"
    " method TEXT NOT NULL DEFAULT 'POST',"
    " status INTEGER NOT NULL DEFAULT 0,"
    " latency_ms INTEGER NOT NULL DEFAULT 0,"
    " request_bytes INTEGER NOT NULL DEFAULT 0,"
    " response_bytes INTEGER NOT NULL DEFAULT 0,"
    " ok INTEGER NOT NULL DEFAULT 1, error TEXT);",
    "CREATE INDEX idx_api_calls_ts ON api_calls(ts);",
    "CREATE TABLE request_logs ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " ts INTEGER NOT NULL, path TEXT NOT NULL, method TEXT NOT NULL,"
    " status INTEGER NOT NULL DEFAULT 0,"
    " latency_ms INTEGER NOT NULL DEFAULT 0,"
    " remote_ip TEXT, user_agent TEXT);",
)


def _day_ago(days: int) -> int:
    return int(time.time()) - days * 24 * 3600


def _seed(conn: sqlite3.Connection) -> None:
    """Deterministic seed covering every chart the dashboard renders."""
    cur = conn.cursor()

    # Sessions: 2/day for the last 3 days, varying duration.
    for d in (2, 1, 0):
        started = _day_ago(d) + 9 * 3600
        cur.execute(
            "INSERT INTO sessions (mode, started_at, ended_at) VALUES (?, ?, ?);",
            ("lesson", started, started + 900),
        )
        cur.execute(
            "INSERT INTO sessions (mode, started_at, ended_at) VALUES (?, ?, ?);",
            ("drill", started + 1000, started + 1600),
        )

    # Interactions: 3 today.
    now = int(time.time())
    for i in range(3):
        cur.execute(
            "INSERT INTO interactions "
            "(session_id, user_text, corrected_text, grammar_notes, created_at) "
            "VALUES (?, ?, ?, ?, ?);",
            (1, f"hello {i}", f"Hello {i}", "note", now - i * 60),
        )

    # Vocab: 5 words, 2 before window, 3 within last 3 days.
    cur.executemany(
        "INSERT INTO vocab_progress "
        "(word, first_seen_at, last_seen_at, seen_count, mastery) VALUES (?,?,?,?,?);",
        [
            ("alpha",   _day_ago(30), _day_ago(1), 8, 2),
            ("beta",    _day_ago(20), _day_ago(1), 5, 1),
            ("gamma",   _day_ago(2),  _day_ago(0), 3, 0),
            ("delta",   _day_ago(1),  _day_ago(0), 2, 0),
            ("epsilon", _day_ago(0),  _day_ago(0), 1, 0),
        ],
    )

    # Pronunciation: 2 attempts across 2 days.
    cur.execute(
        "INSERT INTO pronunciation_attempts "
        "(session_id, reference, transcript, pron_overall, intonation_overall, "
        " per_phoneme_json, created_at) VALUES (?,?,?,?,?,?,?);",
        (1, "good morning", "good morning", 78.5, 72.0, "{}", _day_ago(1)),
    )
    cur.execute(
        "INSERT INTO pronunciation_attempts "
        "(session_id, reference, transcript, pron_overall, intonation_overall, "
        " per_phoneme_json, created_at) VALUES (?,?,?,?,?,?,?);",
        (1, "how are you", "how are you", 85.0, 80.0, "{}", _day_ago(0)),
    )

    # Phoneme mastery: a low scorer and a high scorer.
    cur.executemany(
        "INSERT INTO phoneme_mastery (ipa, attempts, avg_score, last_seen_at) "
        "VALUES (?, ?, ?, ?);",
        [
            ("θ", 10, 45.0, now),
            ("s", 20, 82.0, now),
            ("æ", 4,  55.0, now),
        ],
    )

    # API calls: 4 outbound, spread across 3 days, mix of providers and 1 error.
    cur.executemany(
        "INSERT INTO api_calls "
        "(ts, provider, endpoint, method, status, latency_ms, "
        " request_bytes, response_bytes, ok, error) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [
            (_day_ago(2) + 100, "chat",      "https://api.openai.com/v1/chat", "POST", 200, 480, 256, 1200, 1, None),
            (_day_ago(1) + 100, "chat",      "https://api.openai.com/v1/chat", "POST", 200, 512, 260, 1300, 1, None),
            (_day_ago(1) + 200, "embedding", "https://api.openai.com/v1/embed","POST", 200, 220, 512,  800, 1, None),
            (_day_ago(0) + 100, "chat",      "https://api.openai.com/v1/chat", "POST", 500, 900, 260,    0, 0, "http_500"),
        ],
    )

    # Request logs: 5 recent inbound requests.
    for i in range(5):
        cur.execute(
            "INSERT INTO request_logs "
            "(ts, path, method, status, latency_ms, remote_ip, user_agent) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);",
            (now - i * 30, "/", "GET", 200, 8 + i, "127.0.0.1", "pytest"),
        )

    conn.commit()


@pytest.fixture
def seeded_db(tmp_path: Path) -> Path:
    """Return a path to a fresh SQLite DB with the v2 schema + seed rows."""
    db_path = tmp_path / "hecquin_test.db"
    conn = sqlite3.connect(db_path)
    try:
        for stmt in _DDL:
            conn.execute(stmt)
        _seed(conn)
    finally:
        conn.close()
    return db_path


@pytest.fixture
def settings(seeded_db: Path) -> Iterator[Settings]:
    """Return a Settings instance pointing at the seeded DB, with auth off."""
    cfg = Settings(
        db_path=seeded_db,
        host="127.0.0.1",
        port=8080,
        ssl_cert=None,
        ssl_key=None,
        auth_token="",
        cors_origins="",
        log_level="WARNING",
        default_days=30,
    )
    get_settings.cache_clear()
    yield cfg
    get_settings.cache_clear()


@pytest.fixture
def app(settings: Settings):
    return create_app(settings)


@pytest.fixture
def client(app):
    from fastapi.testclient import TestClient
    with TestClient(app) as c:
        yield c
