"""Repository layer — one class per SQLite table.

Repositories hold **only** data access. Aggregation, bucketing, and any
business logic live in `services/`. This keeps tests tight: repositories are
tested against a seeded temp SQLite; services are tested with fake repos.
"""

from .api_calls_repo import ApiCallRow, ApiCallsRepo
from .base import BaseRepo
from .interactions_repo import InteractionRow, InteractionsRepo
from .phoneme_mastery_repo import PhonemeMasteryRepo, PhonemeMasteryRow
from .pronunciation_repo import PronunciationRepo, PronunciationRow
from .request_logs_repo import RequestLogRow, RequestLogsRepo
from .sessions_repo import SessionRow, SessionsRepo
from .vocab_repo import VocabRepo, VocabRow

__all__ = [
    "ApiCallsRepo",
    "ApiCallRow",
    "BaseRepo",
    "InteractionsRepo",
    "InteractionRow",
    "PhonemeMasteryRepo",
    "PhonemeMasteryRow",
    "PronunciationRepo",
    "PronunciationRow",
    "RequestLogsRepo",
    "RequestLogRow",
    "SessionsRepo",
    "SessionRow",
    "VocabRepo",
    "VocabRow",
]
