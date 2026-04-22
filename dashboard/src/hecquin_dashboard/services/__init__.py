"""Aggregation and chart-building services."""

from .chart_builder import (
    ChartStrategy,
    LineChartStrategy,
    StackedBarChartStrategy,
    build_chart,
)
from .stats_service import StatsService

__all__ = [
    "ChartStrategy",
    "LineChartStrategy",
    "StackedBarChartStrategy",
    "StatsService",
    "build_chart",
]
