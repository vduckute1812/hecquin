"""Access the `phoneme_mastery` roll-up table."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class PhonemeMasteryRow:
    ipa: str
    attempts: int
    avg_score: float
    last_seen_at: int


class PhonemeMasteryRepo(BaseRepo):
    def weakest(self, limit: int = 15, min_attempts: int = 3) -> list[PhonemeMasteryRow]:
        """Phonemes with the lowest mastery — prime candidates for drill."""
        rows = self._fetch_all(
            "SELECT ipa, attempts, avg_score, last_seen_at "
            "FROM phoneme_mastery WHERE attempts >= ? "
            "ORDER BY avg_score ASC, attempts DESC LIMIT ?;",
            (int(min_attempts), int(limit)),
        )
        return self._map(rows, lambda r: PhonemeMasteryRow(
            ipa=r["ipa"], attempts=int(r["attempts"]),
            avg_score=float(r["avg_score"] or 0.0),
            last_seen_at=int(r["last_seen_at"]),
        ))

    def all(self) -> list[PhonemeMasteryRow]:
        rows = self._fetch_all(
            "SELECT ipa, attempts, avg_score, last_seen_at "
            "FROM phoneme_mastery ORDER BY ipa ASC;"
        )
        return self._map(rows, lambda r: PhonemeMasteryRow(
            ipa=r["ipa"], attempts=int(r["attempts"]),
            avg_score=float(r["avg_score"] or 0.0),
            last_seen_at=int(r["last_seen_at"]),
        ))
