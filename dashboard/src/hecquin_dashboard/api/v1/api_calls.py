"""API-call telemetry endpoints (outbound LLM/embedding + inbound dashboard)."""

from __future__ import annotations

from fastapi import APIRouter, Depends, Query

from ...schemas.stats import ApiUsageOverview, RecentApiCall, RecentRequest
from ...services import StatsService
from .deps import get_stats_service

router = APIRouter(prefix="/api-calls", tags=["api-calls"])


@router.get("/daily", response_model=ApiUsageOverview)
def daily(
    days: int = Query(default=30, ge=1, le=365),
    svc: StatsService = Depends(get_stats_service),
) -> ApiUsageOverview:
    """Return outbound + inbound daily bar/line + top endpoints."""
    return svc.api_usage(days=days)


@router.get("/recent/outbound", response_model=list[RecentApiCall])
def recent_outbound(
    limit: int = Query(default=50, ge=1, le=500),
    svc: StatsService = Depends(get_stats_service),
) -> list[RecentApiCall]:
    return svc.recent_api_calls(limit=limit)


@router.get("/recent/inbound", response_model=list[RecentRequest])
def recent_inbound(
    limit: int = Query(default=50, ge=1, le=500),
    svc: StatsService = Depends(get_stats_service),
) -> list[RecentRequest]:
    return svc.recent_requests(limit=limit)
