"""Inbound request logging middleware.

Every HTTP request that reaches the app is timed and its summary is stored in
the ``request_logs`` table (schema created by the C++ migration). The write is
best-effort: any DB error is swallowed so telemetry can never break traffic.
"""

from __future__ import annotations

import logging
import time

from starlette.middleware.base import BaseHTTPMiddleware
from starlette.requests import Request
from starlette.responses import Response

from ..db import WriterConnection

_log = logging.getLogger(__name__)


class RequestLoggerMiddleware(BaseHTTPMiddleware):
    """Capture (ts, path, method, status, latency_ms, ip, ua) per request."""

    # Paths we never log: avoid filling the table with static asset noise and
    # recursion-prone health checks.
    _EXCLUDED_PREFIXES: tuple[str, ...] = ("/static/", "/favicon")

    def __init__(self, app, writer: WriterConnection) -> None:
        super().__init__(app)
        self._writer = writer

    async def dispatch(self, request: Request, call_next) -> Response:
        if any(request.url.path.startswith(p) for p in self._EXCLUDED_PREFIXES):
            return await call_next(request)

        started = time.perf_counter()
        status = 500
        try:
            response = await call_next(request)
            status = response.status_code
            return response
        finally:
            latency_ms = int((time.perf_counter() - started) * 1000)
            self._record(request, status, latency_ms)

    def _record(self, request: Request, status: int, latency_ms: int) -> None:
        try:
            self._writer.execute(
                "INSERT INTO request_logs "
                "(ts, path, method, status, latency_ms, remote_ip, user_agent) "
                "VALUES (strftime('%s','now'), ?, ?, ?, ?, ?, ?);",
                (
                    request.url.path,
                    request.method,
                    int(status),
                    int(latency_ms),
                    request.client.host if request.client else None,
                    request.headers.get("user-agent"),
                ),
            )
        except Exception as exc:  # pragma: no cover — logging never throws
            _log.debug("request_logs insert failed: %s", exc)
