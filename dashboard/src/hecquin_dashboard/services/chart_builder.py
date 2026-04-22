"""Strategy-based Chart.js payload assembly.

Each strategy takes a raw list of ``(day, series_key, value)`` tuples and
returns a fully-formed ``ChartJsPayload`` the frontend can render verbatim.

Colours are deterministic per series key so the same provider gets the same
colour across every chart.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections import OrderedDict
from collections.abc import Iterable
from dataclasses import dataclass

from ..schemas.chart import ChartJsDataset, ChartJsPayload


@dataclass(frozen=True, slots=True)
class Point:
    day: str
    series: str
    value: float


# Chart.js recommends distinct high-contrast palette for up to ~10 series;
# we cycle through this order deterministically keyed by series name.
_PALETTE: tuple[str, ...] = (
    "#4c9aff",  # blue
    "#ff7043",  # orange
    "#66bb6a",  # green
    "#ab47bc",  # purple
    "#ffa726",  # amber
    "#ef5350",  # red
    "#26c6da",  # cyan
    "#8d6e63",  # brown
    "#9ccc65",  # lime
    "#ec407a",  # pink
)


def _colour_for(series: str) -> str:
    # Hash to stable index. Not cryptographic; determinism is all we need.
    return _PALETTE[abs(hash(series)) % len(_PALETTE)]


def _sorted_unique_days(points: Iterable[Point]) -> list[str]:
    return sorted({p.day for p in points})


class ChartStrategy(ABC):
    """Common interface for every chart assembly strategy."""

    @abstractmethod
    def build(self, points: list[Point], title: str) -> ChartJsPayload: ...


class LineChartStrategy(ChartStrategy):
    """One line per ``series`` — gaps are rendered as zeros."""

    def __init__(self, fill: bool = False, tension: float = 0.3) -> None:
        self._fill = fill
        self._tension = tension

    def build(self, points: list[Point], title: str) -> ChartJsPayload:
        labels = _sorted_unique_days(points)
        by_series: OrderedDict[str, dict[str, float]] = OrderedDict()
        for p in points:
            by_series.setdefault(p.series, {})[p.day] = p.value

        datasets = [
            ChartJsDataset(
                label=series,
                data=[values.get(day, 0.0) for day in labels],
                border_color=_colour_for(series),
                background_color=_colour_for(series),
                fill=self._fill,
                tension=self._tension,
            )
            for series, values in by_series.items()
        ]
        return ChartJsPayload(
            type="line",
            labels=labels,
            datasets=datasets,
            options={
                "responsive": True,
                "plugins": {"title": {"display": bool(title), "text": title}},
                "scales": {"y": {"beginAtZero": True}},
            },
        )


class StackedBarChartStrategy(ChartStrategy):
    """Stacked bar — e.g. API calls/day grouped by provider."""

    def build(self, points: list[Point], title: str) -> ChartJsPayload:
        labels = _sorted_unique_days(points)
        by_series: OrderedDict[str, dict[str, float]] = OrderedDict()
        for p in points:
            by_series.setdefault(p.series, {})[p.day] = p.value

        datasets = [
            ChartJsDataset(
                label=series,
                data=[values.get(day, 0.0) for day in labels],
                background_color=_colour_for(series),
                border_color=_colour_for(series),
                stack="total",
            )
            for series, values in by_series.items()
        ]
        return ChartJsPayload(
            type="bar",
            labels=labels,
            datasets=datasets,
            options={
                "responsive": True,
                "plugins": {"title": {"display": bool(title), "text": title}},
                "scales": {
                    "x": {"stacked": True},
                    "y": {"stacked": True, "beginAtZero": True},
                },
            },
        )


_STRATEGIES: dict[str, ChartStrategy] = {
    "line": LineChartStrategy(),
    "line_fill": LineChartStrategy(fill=True, tension=0.35),
    "stacked_bar": StackedBarChartStrategy(),
}


def build_chart(kind: str, points: list[Point], title: str = "") -> ChartJsPayload:
    """Dispatch to a named strategy; raises `KeyError` on unknown kind."""
    return _STRATEGIES[kind].build(points, title)
