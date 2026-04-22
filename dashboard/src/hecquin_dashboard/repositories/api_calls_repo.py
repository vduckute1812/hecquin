"""Access the `api_calls` table — outbound LLM / embedding traffic."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class ApiCallRow:
    id: int
    ts: int
    provider: str
    endpoint: str
    method: str
    status: int
    latency_ms: int
    request_bytes: int
    response_bytes: int
    ok: bool
    error: str | None


@dataclass(frozen=True, slots=True)
class DailyApiBucket:
    day: str
    provider: str
    count: int
    errors: int
    avg_latency_ms: float


@dataclass(frozen=True, slots=True)
class EndpointStat:
    endpoint: str
    provider: str
    count: int
    avg_latency_ms: float
    error_rate: float


class ApiCallsRepo(BaseRepo):
    def count_since(self, epoch_seconds: int) -> int:
        return int(self._scalar(
            "SELECT COUNT(*) FROM api_calls WHERE ts >= ?;",
            (int(epoch_seconds),),
        ))

    def daily_by_provider(self, since_epoch: int) -> list[DailyApiBucket]:
        rows = self._fetch_all(
            "SELECT strftime('%Y-%m-%d', ts, 'unixepoch') AS day, "
            "       provider, "
            "       COUNT(*) AS cnt, "
            "       SUM(CASE WHEN ok = 0 THEN 1 ELSE 0 END) AS err_cnt, "
            "       COALESCE(AVG(latency_ms), 0) AS avg_lat "
            "FROM api_calls WHERE ts >= ? "
            "GROUP BY day, provider ORDER BY day ASC, provider ASC;",
            (int(since_epoch),),
        )
        return self._map(rows, lambda r: DailyApiBucket(
            day=r["day"], provider=r["provider"],
            count=int(r["cnt"]), errors=int(r["err_cnt"] or 0),
            avg_latency_ms=float(r["avg_lat"] or 0.0),
        ))

    def top_endpoints(self, since_epoch: int, limit: int = 10) -> list[EndpointStat]:
        rows = self._fetch_all(
            "SELECT endpoint, provider, "
            "       COUNT(*) AS cnt, "
            "       COALESCE(AVG(latency_ms), 0) AS avg_lat, "
            "       AVG(CASE WHEN ok = 0 THEN 1.0 ELSE 0.0 END) AS err_rate "
            "FROM api_calls WHERE ts >= ? "
            "GROUP BY endpoint, provider ORDER BY cnt DESC LIMIT ?;",
            (int(since_epoch), int(limit)),
        )
        return self._map(rows, lambda r: EndpointStat(
            endpoint=r["endpoint"], provider=r["provider"],
            count=int(r["cnt"]),
            avg_latency_ms=float(r["avg_lat"] or 0.0),
            error_rate=float(r["err_rate"] or 0.0),
        ))

    def recent(self, limit: int = 50) -> list[ApiCallRow]:
        rows = self._fetch_all(
            "SELECT id, ts, provider, endpoint, method, status, latency_ms, "
            "       request_bytes, response_bytes, ok, error "
            "FROM api_calls ORDER BY ts DESC LIMIT ?;",
            (int(limit),),
        )
        return self._map(rows, lambda r: ApiCallRow(
            id=int(r["id"]), ts=int(r["ts"]),
            provider=r["provider"], endpoint=r["endpoint"],
            method=r["method"], status=int(r["status"]),
            latency_ms=int(r["latency_ms"]),
            request_bytes=int(r["request_bytes"]),
            response_bytes=int(r["response_bytes"]),
            ok=bool(r["ok"]), error=r["error"],
        ))
