"""Learning-progress endpoints: vocab, sessions, pronunciation, phonemes."""

from __future__ import annotations

from fastapi import APIRouter, Depends, Query

from ...schemas.stats import LearningOverview, PhonemeWeakness, TopVocab
from ...services import StatsService
from .deps import get_stats_service

router = APIRouter(prefix="/learning", tags=["learning"])


@router.get("/overview", response_model=LearningOverview)
def overview(
    days: int = Query(default=30, ge=1, le=365),
    svc: StatsService = Depends(get_stats_service),
) -> LearningOverview:
    return svc.learning(days=days)


@router.get("/phonemes/weakest", response_model=list[PhonemeWeakness])
def weakest_phonemes(
    limit: int = Query(default=15, ge=1, le=100),
    svc: StatsService = Depends(get_stats_service),
) -> list[PhonemeWeakness]:
    return svc.weakest_phonemes(limit=limit)


@router.get("/vocab/top", response_model=list[TopVocab])
def top_vocab(
    limit: int = Query(default=25, ge=1, le=200),
    svc: StatsService = Depends(get_stats_service),
) -> list[TopVocab]:
    return svc.top_vocab(limit=limit)
