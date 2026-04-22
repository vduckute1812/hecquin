"""Shared primitive DTOs."""

from __future__ import annotations

from datetime import datetime, timedelta, timezone

from pydantic import BaseModel, Field


class DateBucket(BaseModel):
    """One row in a daily-aggregated series."""

    day: str = Field(..., description="ISO YYYY-MM-DD (UTC)")
    value: float


class TimeRange(BaseModel):
    """Validated ``?days=N`` window. Use `since_epoch` to query the DB."""

    days: int = Field(default=30, ge=1, le=3650)

    @property
    def since_epoch(self) -> int:
        return int(
            (datetime.now(tz=timezone.utc) - timedelta(days=self.days)).timestamp()
        )
