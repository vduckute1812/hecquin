"""Repositories: raw SQL → typed rows / buckets."""

from __future__ import annotations

import time
from pathlib import Path

from hecquin_dashboard.repositories import (
    ApiCallsRepo,
    PhonemeMasteryRepo,
    PronunciationRepo,
    RequestLogsRepo,
    SessionsRepo,
    VocabRepo,
)


def test_sessions_count_since_today(seeded_db: Path):
    repo = SessionsRepo(seeded_db)
    start_of_day = int(time.time()) - 24 * 3600
    assert repo.count_since(start_of_day) >= 2  # today's 2 + yesterday's tail


def test_sessions_daily_buckets(seeded_db: Path):
    repo = SessionsRepo(seeded_db)
    buckets = repo.daily_buckets(int(time.time()) - 7 * 24 * 3600)
    assert len(buckets) >= 1
    for b in buckets:
        assert b.count > 0
        assert b.total_seconds >= 0


def test_vocab_daily_growth_cumulative_includes_baseline(seeded_db: Path):
    repo = VocabRepo(seeded_db)
    since = int(time.time()) - 3 * 24 * 3600 - 60
    buckets = repo.daily_growth(since)
    # 2 words older than the window = baseline 2, plus the 3 newer ones.
    assert buckets, "expected at least one bucket"
    assert buckets[-1].cumulative == 5
    assert repo.total() == 5


def test_pronunciation_avg_score(seeded_db: Path):
    repo = PronunciationRepo(seeded_db)
    avg = repo.avg_score_since(int(time.time()) - 3 * 24 * 3600)
    assert 70.0 <= avg <= 100.0


def test_phoneme_weakest_orders_by_score_asc(seeded_db: Path):
    repo = PhonemeMasteryRepo(seeded_db)
    rows = repo.weakest(limit=5, min_attempts=3)
    assert rows, "expected weak phonemes in fixture"
    assert rows[0].avg_score <= rows[-1].avg_score


def test_api_calls_daily_by_provider(seeded_db: Path):
    repo = ApiCallsRepo(seeded_db)
    buckets = repo.daily_by_provider(int(time.time()) - 3 * 24 * 3600)
    providers = {b.provider for b in buckets}
    assert "chat" in providers
    assert "embedding" in providers
    total = sum(b.count for b in buckets)
    assert total == 4


def test_api_calls_top_endpoints_counts_errors(seeded_db: Path):
    repo = ApiCallsRepo(seeded_db)
    top = repo.top_endpoints(int(time.time()) - 3 * 24 * 3600, limit=5)
    chat_rows = [e for e in top if "chat" in e.endpoint]
    assert chat_rows
    # one of three chat calls was an error → ~33% error rate
    assert chat_rows[0].error_rate > 0


def test_request_logs_daily(seeded_db: Path):
    repo = RequestLogsRepo(seeded_db)
    buckets = repo.daily(int(time.time()) - 24 * 3600)
    assert buckets
    assert sum(b.count for b in buckets) == 5
