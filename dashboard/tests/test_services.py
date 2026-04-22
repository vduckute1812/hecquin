"""Service layer — aggregation + Chart.js DTO shape."""

from __future__ import annotations

from pathlib import Path

from hecquin_dashboard.repositories import (
    ApiCallsRepo,
    InteractionsRepo,
    PhonemeMasteryRepo,
    PronunciationRepo,
    RequestLogsRepo,
    SessionsRepo,
    VocabRepo,
)
from hecquin_dashboard.services import StatsService
from hecquin_dashboard.services.chart_builder import (
    LineChartStrategy,
    Point,
    StackedBarChartStrategy,
)


def _make_service(db_path: Path) -> StatsService:
    return StatsService(
        sessions=SessionsRepo(db_path),
        interactions=InteractionsRepo(db_path),
        vocab=VocabRepo(db_path),
        pronunciation=PronunciationRepo(db_path),
        phonemes=PhonemeMasteryRepo(db_path),
        api_calls=ApiCallsRepo(db_path),
        request_logs=RequestLogsRepo(db_path),
    )


def test_overview_totals_match_seed(seeded_db: Path):
    svc = _make_service(seeded_db)
    overview = svc.overview()
    assert overview.vocab_total == 5
    assert overview.today_api_calls >= 1
    assert overview.today_inbound_requests == 5
    # 7-day average should be the mean of the two fixture scores (78.5 + 85)/2
    assert abs(overview.avg_pron_score_7d - 81.75) < 0.5


def test_api_usage_payload_is_chartjs_shaped(seeded_db: Path):
    svc = _make_service(seeded_db)
    out = svc.api_usage(days=7)
    assert out.outbound_daily.type == "bar"
    assert {"x", "y"}.issubset(out.outbound_daily.options["scales"].keys())
    # every dataset same length as labels
    for ds in out.outbound_daily.datasets:
        assert len(ds.data) == len(out.outbound_daily.labels)
    assert out.totals_outbound == 4


def test_learning_overview_includes_three_charts(seeded_db: Path):
    svc = _make_service(seeded_db)
    out = svc.learning(days=30)
    assert out.vocab_growth.type == "line"
    assert out.sessions_daily.type == "line"
    assert out.pronunciation_daily.type == "line"
    assert {d.label for d in out.vocab_growth.datasets} == {"cumulative", "new"}


def test_line_strategy_fills_gaps_with_zero():
    strategy = LineChartStrategy()
    pts = [
        Point(day="2026-01-01", series="a", value=3.0),
        Point(day="2026-01-03", series="a", value=5.0),
        Point(day="2026-01-02", series="b", value=2.0),
    ]
    payload = strategy.build(pts, title="t")
    assert payload.labels == ["2026-01-01", "2026-01-02", "2026-01-03"]
    a = next(d for d in payload.datasets if d.label == "a")
    b = next(d for d in payload.datasets if d.label == "b")
    assert a.data == [3.0, 0.0, 5.0]
    assert b.data == [0.0, 2.0, 0.0]


def test_stacked_bar_strategy_sets_stack_group():
    strategy = StackedBarChartStrategy()
    pts = [
        Point(day="d1", series="openai", value=2.0),
        Point(day="d1", series="gemini", value=3.0),
    ]
    payload = strategy.build(pts, title="stack")
    assert payload.type == "bar"
    for ds in payload.datasets:
        assert ds.stack == "total"
    assert payload.options["scales"]["y"]["stacked"] is True
