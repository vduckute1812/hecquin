"""Access the `request_logs` table — inbound dashboard traffic."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class RequestLogRow:
    id: int
    ts: int
    path: str
    method: str
    status: int
    latency_ms: int
    remote_ip: str | None
    user_agent: str | None


@dataclass(frozen=True, slots=True)
class DailyRequestBucket:
    day: str
    count: int
    errors: int
    avg_latency_ms: float


class RequestLogsRepo(BaseRepo):
    def count_since(self, epoch_seconds: int) -> int:
        return int(self._scalar(
            "SELECT COUNT(*) FROM request_logs WHERE ts >= ?;",
            (int(epoch_seconds),),
        ))

    def daily(self, since_epoch: int) -> list[DailyRequestBucket]:
        rows = self._fetch_all(
            "SELECT strftime('%Y-%m-%d', ts, 'unixepoch') AS day, "
            "       COUNT(*) AS cnt, "
            "       SUM(CASE WHEN status >= 400 THEN 1 ELSE 0 END) AS err_cnt, "
            "       COALESCE(AVG(latency_ms), 0) AS avg_lat "
            "FROM request_logs WHERE ts >= ? "
            "GROUP BY day ORDER BY day ASC;",
            (int(since_epoch),),
        )
        return self._map(rows, lambda r: DailyRequestBucket(
            day=r["day"], count=int(r["cnt"]),
            errors=int(r["err_cnt"] or 0),
            avg_latency_ms=float(r["avg_lat"] or 0.0),
        ))

    def recent(self, limit: int = 50) -> list[RequestLogRow]:
        rows = self._fetch_all(
            "SELECT id, ts, path, method, status, latency_ms, remote_ip, user_agent "
            "FROM request_logs ORDER BY ts DESC LIMIT ?;",
            (int(limit),),
        )
        return self._map(rows, lambda r: RequestLogRow(
            id=int(r["id"]), ts=int(r["ts"]),
            path=r["path"], method=r["method"],
            status=int(r["status"]), latency_ms=int(r["latency_ms"]),
            remote_ip=r["remote_ip"], user_agent=r["user_agent"],
        ))
