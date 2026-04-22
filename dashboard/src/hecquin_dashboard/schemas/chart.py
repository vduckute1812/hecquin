"""Chart.js-compatible envelopes.

Kept minimal: the shape is literally what Chart.js 4.x expects so the
frontend can pass the payload straight to ``new Chart(ctx, payload)``.
"""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class ChartJsDataset(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    label: str
    data: list[float]
    background_color: str | list[str] | None = Field(default=None, alias="backgroundColor")
    border_color: str | list[str] | None = Field(default=None, alias="borderColor")
    fill: bool | str | None = None
    stack: str | None = None
    tension: float | None = None
    type: str | None = None
    y_axis_id: str | None = Field(default=None, alias="yAxisID")


class ChartJsPayload(BaseModel):
    """A single ``type + data.labels + data.datasets + options`` bundle."""

    model_config = ConfigDict(populate_by_name=True)

    type: str
    labels: list[str]
    datasets: list[ChartJsDataset]
    options: dict[str, Any] | None = None
