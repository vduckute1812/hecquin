"""FastAPI dependency factories — wire repositories + services to `Settings`.

Centralising construction here keeps route handlers declarative:

    @router.get("/foo")
    def foo(service: StatsService = Depends(get_stats_service)):
        return service.overview()

Tests override these via ``app.dependency_overrides[get_stats_service] = ...``.
"""

from __future__ import annotations

from fastapi import Depends, Request

from ...config import Settings
from ...repositories import (
    ApiCallsRepo,
    InteractionsRepo,
    PhonemeMasteryRepo,
    PronunciationRepo,
    RequestLogsRepo,
    SessionsRepo,
    VocabRepo,
)
from ...services import StatsService


def get_settings_dep(request: Request) -> Settings:
    return request.app.state.settings


def get_stats_service(
    settings: Settings = Depends(get_settings_dep),
) -> StatsService:
    path = settings.db_path
    return StatsService(
        sessions=SessionsRepo(path),
        interactions=InteractionsRepo(path),
        vocab=VocabRepo(path),
        pronunciation=PronunciationRepo(path),
        phonemes=PhonemeMasteryRepo(path),
        api_calls=ApiCallsRepo(path),
        request_logs=RequestLogsRepo(path),
    )
