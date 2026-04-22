"""High-level aggregator.

Sits above the repositories and produces DTOs consumed by both the API
routes and the Jinja views. Routes never touch repos directly — that would
duplicate aggregation logic and complicate testing.
"""

from __future__ import annotations

from datetime import datetime, timedelta, timezone

from ..repositories import (
    ApiCallsRepo,
    InteractionsRepo,
    PhonemeMasteryRepo,
    PronunciationRepo,
    RequestLogsRepo,
    SessionsRepo,
    VocabRepo,
)
from ..schemas.stats import (
    ApiUsageOverview,
    EndpointUsage,
    LearningOverview,
    Overview,
    PhonemeWeakness,
    RecentApiCall,
    RecentRequest,
    TopVocab,
)
from .chart_builder import Point, build_chart


def _start_of_today_utc() -> int:
    now = datetime.now(tz=timezone.utc)
    midnight = datetime(now.year, now.month, now.day, tzinfo=timezone.utc)
    return int(midnight.timestamp())


def _since(days: int) -> int:
    return int(
        (datetime.now(tz=timezone.utc) - timedelta(days=days)).timestamp()
    )


class StatsService:
    """Aggregates repository output into chart-ready DTOs."""

    def __init__(
        self,
        sessions: SessionsRepo,
        interactions: InteractionsRepo,
        vocab: VocabRepo,
        pronunciation: PronunciationRepo,
        phonemes: PhonemeMasteryRepo,
        api_calls: ApiCallsRepo,
        request_logs: RequestLogsRepo,
    ) -> None:
        self._sessions = sessions
        self._interactions = interactions
        self._vocab = vocab
        self._pronunciation = pronunciation
        self._phonemes = phonemes
        self._api_calls = api_calls
        self._request_logs = request_logs

    # ---- Overview ---------------------------------------------------------
    def overview(self) -> Overview:
        today = _start_of_today_utc()
        return Overview(
            today_api_calls=self._api_calls.count_since(today),
            today_inbound_requests=self._request_logs.count_since(today),
            today_sessions=self._sessions.count_since(today),
            avg_pron_score_7d=round(
                self._pronunciation.avg_score_since(_since(7)), 2
            ),
            vocab_total=self._vocab.total(),
        )

    # ---- API usage --------------------------------------------------------
    def api_usage(self, days: int) -> ApiUsageOverview:
        since = _since(days)

        outbound = self._api_calls.daily_by_provider(since)
        outbound_points = [
            Point(day=b.day, series=b.provider, value=float(b.count))
            for b in outbound
        ]
        inbound = self._request_logs.daily(since)
        inbound_points = [
            Point(day=b.day, series="dashboard", value=float(b.count))
            for b in inbound
        ]

        top = self._api_calls.top_endpoints(since, limit=10)

        return ApiUsageOverview(
            outbound_daily=build_chart(
                "stacked_bar", outbound_points,
                title=f"Outbound API calls — last {days}d",
            ),
            inbound_daily=build_chart(
                "line_fill", inbound_points,
                title=f"Inbound dashboard requests — last {days}d",
            ),
            top_endpoints=[
                EndpointUsage(
                    endpoint=e.endpoint, provider=e.provider, count=e.count,
                    avg_latency_ms=round(e.avg_latency_ms, 1),
                    error_rate=round(e.error_rate, 3),
                )
                for e in top
            ],
            totals_outbound=sum(b.count for b in outbound),
            totals_inbound=sum(b.count for b in inbound),
        )

    # ---- Learning ---------------------------------------------------------
    def learning(self, days: int) -> LearningOverview:
        since = _since(days)

        vocab = self._vocab.daily_growth(since)
        vocab_points = [
            Point(day=b.day, series="cumulative", value=float(b.cumulative))
            for b in vocab
        ] + [
            Point(day=b.day, series="new", value=float(b.new_words))
            for b in vocab
        ]

        sessions = self._sessions.daily_buckets(since)
        sessions_points = [
            Point(day=b.day, series="sessions", value=float(b.count))
            for b in sessions
        ] + [
            Point(day=b.day, series="minutes", value=round(b.total_seconds / 60.0, 1))
            for b in sessions
        ]

        pron = self._pronunciation.daily_scores(since)
        pron_points = [
            Point(day=b.day, series="pronunciation", value=round(b.avg_pron, 1))
            for b in pron
        ] + [
            Point(day=b.day, series="intonation", value=round(b.avg_intonation, 1))
            for b in pron
        ]

        return LearningOverview(
            vocab_growth=build_chart(
                "line", vocab_points, title=f"Vocabulary — last {days}d"
            ),
            sessions_daily=build_chart(
                "line", sessions_points, title=f"Sessions — last {days}d"
            ),
            pronunciation_daily=build_chart(
                "line", pron_points, title=f"Pronunciation score — last {days}d"
            ),
            vocab_total=self._vocab.total(),
        )

    # ---- Tables -----------------------------------------------------------
    def weakest_phonemes(self, limit: int = 15) -> list[PhonemeWeakness]:
        return [
            PhonemeWeakness(
                ipa=p.ipa, attempts=p.attempts,
                avg_score=round(p.avg_score, 2),
            )
            for p in self._phonemes.weakest(limit=limit)
        ]

    def top_vocab(self, limit: int = 25) -> list[TopVocab]:
        return [
            TopVocab(word=v.word, seen_count=v.seen_count, mastery=v.mastery)
            for v in self._vocab.top_seen(limit=limit)
        ]

    def recent_api_calls(self, limit: int = 50) -> list[RecentApiCall]:
        return [
            RecentApiCall(
                ts=c.ts, provider=c.provider, endpoint=c.endpoint,
                status=c.status, latency_ms=c.latency_ms,
                ok=c.ok, error=c.error,
            )
            for c in self._api_calls.recent(limit=limit)
        ]

    def recent_requests(self, limit: int = 50) -> list[RecentRequest]:
        return [
            RecentRequest(
                ts=r.ts, path=r.path, method=r.method, status=r.status,
                latency_ms=r.latency_ms, remote_ip=r.remote_ip,
            )
            for r in self._request_logs.recent(limit=limit)
        ]
