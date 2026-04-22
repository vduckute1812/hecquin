"""Access the `pronunciation_attempts` table."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class PronunciationRow:
    id: int
    session_id: int | None
    reference: str
    transcript: str | None
    pron_overall: float | None
    intonation_overall: float | None
    per_phoneme_json: str | None
    created_at: int


@dataclass(frozen=True, slots=True)
class DailyPronunciationBucket:
    day: str
    attempts: int
    avg_pron: float
    avg_intonation: float


class PronunciationRepo(BaseRepo):
    def recent(self, limit: int = 20) -> list[PronunciationRow]:
        rows = self._fetch_all(
            "SELECT id, session_id, reference, transcript, pron_overall, "
            "       intonation_overall, per_phoneme_json, created_at "
            "FROM pronunciation_attempts ORDER BY created_at DESC LIMIT ?;",
            (int(limit),),
        )
        return self._map(rows, lambda r: PronunciationRow(
            id=r["id"], session_id=r["session_id"],
            reference=r["reference"], transcript=r["transcript"],
            pron_overall=r["pron_overall"],
            intonation_overall=r["intonation_overall"],
            per_phoneme_json=r["per_phoneme_json"],
            created_at=r["created_at"],
        ))

    def avg_score_since(self, epoch_seconds: int) -> float:
        return float(self._scalar(
            "SELECT AVG(pron_overall) FROM pronunciation_attempts "
            "WHERE created_at >= ? AND pron_overall IS NOT NULL;",
            (int(epoch_seconds),), default=0.0,
        ) or 0.0)

    def daily_scores(self, since_epoch: int) -> list[DailyPronunciationBucket]:
        rows = self._fetch_all(
            "SELECT strftime('%Y-%m-%d', created_at, 'unixepoch') AS day, "
            "       COUNT(*) AS cnt, "
            "       COALESCE(AVG(pron_overall), 0)       AS avg_p, "
            "       COALESCE(AVG(intonation_overall), 0) AS avg_i "
            "FROM pronunciation_attempts WHERE created_at >= ? "
            "GROUP BY day ORDER BY day;",
            (int(since_epoch),),
        )
        return self._map(rows, lambda r: DailyPronunciationBucket(
            day=r["day"], attempts=int(r["cnt"]),
            avg_pron=float(r["avg_p"] or 0.0),
            avg_intonation=float(r["avg_i"] or 0.0),
        ))
