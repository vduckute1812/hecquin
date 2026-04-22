"""Access the `interactions` table (tutor request/response pairs)."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class InteractionRow:
    id: int
    session_id: int | None
    user_text: str
    corrected_text: str | None
    grammar_notes: str | None
    created_at: int


class InteractionsRepo(BaseRepo):
    def recent(self, limit: int = 50) -> list[InteractionRow]:
        rows = self._fetch_all(
            "SELECT id, session_id, user_text, corrected_text, grammar_notes, created_at "
            "FROM interactions ORDER BY created_at DESC LIMIT ?;",
            (int(limit),),
        )
        return self._map(rows, lambda r: InteractionRow(
            id=r["id"], session_id=r["session_id"],
            user_text=r["user_text"], corrected_text=r["corrected_text"],
            grammar_notes=r["grammar_notes"], created_at=r["created_at"],
        ))

    def count_since(self, epoch_seconds: int) -> int:
        return int(self._scalar(
            "SELECT COUNT(*) FROM interactions WHERE created_at >= ?;",
            (int(epoch_seconds),),
        ))
