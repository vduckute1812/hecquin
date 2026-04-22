"""Access the `sessions` table (one row per tutor / drill session)."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class SessionRow:
    id: int
    mode: str
    started_at: int
    ended_at: int | None


@dataclass(frozen=True, slots=True)
class DailySessionBucket:
    day: str           # ISO YYYY-MM-DD (UTC)
    count: int
    total_seconds: int


class SessionsRepo(BaseRepo):
    def recent(self, limit: int = 20) -> list[SessionRow]:
        rows = self._fetch_all(
            "SELECT id, mode, started_at, ended_at FROM sessions "
            "ORDER BY started_at DESC LIMIT ?;",
            (int(limit),),
        )
        return self._map(rows, lambda r: SessionRow(
            id=r["id"], mode=r["mode"],
            started_at=r["started_at"], ended_at=r["ended_at"],
        ))

    def count_since(self, epoch_seconds: int) -> int:
        return int(self._scalar(
            "SELECT COUNT(*) FROM sessions WHERE started_at >= ?;",
            (int(epoch_seconds),),
        ))

    def daily_buckets(self, since_epoch: int) -> list[DailySessionBucket]:
        """Sessions/day + total active seconds (end - start, 0 if still open)."""
        rows = self._fetch_all(
            "SELECT strftime('%Y-%m-%d', started_at, 'unixepoch') AS day, "
            "       COUNT(*) AS cnt, "
            "       COALESCE(SUM(CASE WHEN ended_at IS NOT NULL "
            "                         THEN ended_at - started_at ELSE 0 END), 0) AS dur "
            "FROM sessions WHERE started_at >= ? "
            "GROUP BY day ORDER BY day;",
            (int(since_epoch),),
        )
        return self._map(rows, lambda r: DailySessionBucket(
            day=r["day"], count=int(r["cnt"]), total_seconds=int(r["dur"]),
        ))
