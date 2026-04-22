"""Bearer-token auth middleware.

If a non-empty ``auth_token`` is configured, every request must carry either:

* ``Authorization: Bearer <token>`` (for API clients), or
* a cookie ``hecquin_auth=<token>`` (so the browser can keep the dashboard
  pages logged in after the user enters the token once).

``/health`` and ``/static/*`` are always public.
"""

from __future__ import annotations

import hmac

from starlette.middleware.base import BaseHTTPMiddleware
from starlette.requests import Request
from starlette.responses import JSONResponse, Response

_PUBLIC_PREFIXES: tuple[str, ...] = ("/health", "/static/", "/favicon")


class BearerAuthMiddleware(BaseHTTPMiddleware):
    """Gate everything except public paths behind a constant-time token check."""

    def __init__(self, app, token: str) -> None:
        super().__init__(app)
        self._token = token or ""

    async def dispatch(self, request: Request, call_next) -> Response:
        if not self._token:
            return await call_next(request)

        if any(request.url.path.startswith(p) for p in _PUBLIC_PREFIXES):
            return await call_next(request)

        supplied = self._extract(request)
        # hmac.compare_digest guards against timing attacks.
        if not supplied or not hmac.compare_digest(supplied, self._token):
            return JSONResponse(
                {"detail": "unauthorised"},
                status_code=401,
                headers={"WWW-Authenticate": "Bearer"},
            )
        return await call_next(request)

    @staticmethod
    def _extract(request: Request) -> str | None:
        header = request.headers.get("authorization")
        if header and header.lower().startswith("bearer "):
            return header[7:].strip()
        cookie = request.cookies.get("hecquin_auth")
        if cookie:
            return cookie.strip()
        return None
