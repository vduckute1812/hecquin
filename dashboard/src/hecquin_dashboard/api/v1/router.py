"""Top-level router — include per-feature sub-routers here."""

from __future__ import annotations

from fastapi import APIRouter

from . import api_calls, learning, overview

router = APIRouter()
router.include_router(overview.router)
router.include_router(api_calls.router)
router.include_router(learning.router)
