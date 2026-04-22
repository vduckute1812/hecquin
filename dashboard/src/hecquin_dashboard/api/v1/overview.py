"""Aggregated KPI endpoint used by the dashboard landing page."""

from __future__ import annotations

from fastapi import APIRouter, Depends

from ...schemas.stats import Overview
from ...services import StatsService
from .deps import get_stats_service

router = APIRouter(tags=["overview"])


@router.get("/overview", response_model=Overview)
def get_overview(svc: StatsService = Depends(get_stats_service)) -> Overview:
    return svc.overview()
