"""Jinja-rendered dashboard pages.

Pages stay thin: they render a template shell and let the browser call
``/api/v1/...`` via ``static/js/app.js`` to populate charts. This keeps the
server free from any chart logic duplication.
"""

from __future__ import annotations

from fastapi import APIRouter, Depends, Request
from fastapi.responses import HTMLResponse

from ..api.v1.deps import get_stats_service
from ..services import StatsService

router = APIRouter(tags=["pages"])


def _templates(request: Request):
    return request.app.state.templates


@router.get("/", response_class=HTMLResponse)
def index(request: Request, svc: StatsService = Depends(get_stats_service)):
    return _templates(request).TemplateResponse(
        request=request,
        name="index.html",
        context={"overview": svc.overview()},
    )


@router.get("/api-usage", response_class=HTMLResponse)
def api_usage(request: Request):
    return _templates(request).TemplateResponse(
        request=request,
        name="api_usage.html",
        context={"default_days": request.app.state.settings.default_days},
    )


@router.get("/learning", response_class=HTMLResponse)
def learning(request: Request):
    return _templates(request).TemplateResponse(
        request=request,
        name="learning.html",
        context={"default_days": request.app.state.settings.default_days},
    )


@router.get("/pronunciation", response_class=HTMLResponse)
def pronunciation(request: Request, svc: StatsService = Depends(get_stats_service)):
    return _templates(request).TemplateResponse(
        request=request,
        name="pronunciation.html",
        context={"weakest": svc.weakest_phonemes(limit=20)},
    )
