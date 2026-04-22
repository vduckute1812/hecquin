"""Access the `vocab_progress` table."""

from __future__ import annotations

from dataclasses import dataclass

from .base import BaseRepo


@dataclass(frozen=True, slots=True)
class VocabRow:
    word: str
    first_seen_at: int
    last_seen_at: int
    seen_count: int
    mastery: int


@dataclass(frozen=True, slots=True)
class DailyVocabBucket:
    day: str
    new_words: int
    cumulative: int


class VocabRepo(BaseRepo):
    def total(self) -> int:
        return int(self._scalar("SELECT COUNT(*) FROM vocab_progress;"))

    def top_seen(self, limit: int = 25) -> list[VocabRow]:
        rows = self._fetch_all(
            "SELECT word, first_seen_at, last_seen_at, seen_count, mastery "
            "FROM vocab_progress ORDER BY seen_count DESC, word ASC LIMIT ?;",
            (int(limit),),
        )
        return self._map(rows, lambda r: VocabRow(
            word=r["word"], first_seen_at=r["first_seen_at"],
            last_seen_at=r["last_seen_at"],
            seen_count=int(r["seen_count"]), mastery=int(r["mastery"]),
        ))

    def daily_growth(self, since_epoch: int) -> list[DailyVocabBucket]:
        """New unique words per day (by first_seen_at) + running cumulative."""
        rows = self._fetch_all(
            "SELECT strftime('%Y-%m-%d', first_seen_at, 'unixepoch') AS day, "
            "       COUNT(*) AS new_cnt "
            "FROM vocab_progress WHERE first_seen_at >= ? "
            "GROUP BY day ORDER BY day;",
            (int(since_epoch),),
        )
        # Baseline: words that existed before `since_epoch` so the cumulative
        # curve starts from the correct offset, not zero.
        baseline = int(self._scalar(
            "SELECT COUNT(*) FROM vocab_progress WHERE first_seen_at < ?;",
            (int(since_epoch),),
        ))
        out: list[DailyVocabBucket] = []
        running = baseline
        for r in rows:
            running += int(r["new_cnt"])
            out.append(DailyVocabBucket(
                day=r["day"], new_words=int(r["new_cnt"]), cumulative=running,
            ))
        return out
