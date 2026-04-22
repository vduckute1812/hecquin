"""FastAPI application factory + uvicorn entry point.

Run with:

    python -m hecquin_dashboard.main              # uses pydantic Settings
    hecquin-dashboard                             # installed console script
    uvicorn hecquin_dashboard.main:app \\
        --host 0.0.0.0 --port 443 \\
        --ssl-keyfile certs/server.key --ssl-certfile certs/server.crt
"""

from __future__ import annotations

import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from starlette.middleware.cors import CORSMiddleware

from .api.v1.router import router as api_v1_router
from .config import Settings, get_settings
from .db import WriterConnection
from .middleware.auth import BearerAuthMiddleware
from .middleware.request_logger import RequestLoggerMiddleware
from .views.pages import router as pages_router

_PKG_ROOT = Path(__file__).resolve().parent
_TEMPLATES_DIR = _PKG_ROOT / "templates"
_STATIC_DIR = _PKG_ROOT / "static"


def _configure_logging(level: str) -> None:
    logging.basicConfig(
        level=getattr(logging, level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)-7s %(name)s :: %(message)s",
    )


def create_app(settings: Settings | None = None) -> FastAPI:
    """Build a fully-wired FastAPI instance.

    Splitting construction into a factory makes integration tests trivial —
    they pass a custom `Settings` pointing at a seeded temp SQLite and skip
    auth by leaving `auth_token` empty.
    """
    cfg = settings or get_settings()
    _configure_logging(cfg.log_level)

    writer = WriterConnection(cfg.db_path)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        app.state.settings = cfg
        app.state.writer = writer
        yield
        writer.close()

    app = FastAPI(
        title="Hecquin Dashboard",
        version="0.1.0",
        description="Learning progress + API-call telemetry for the Hecquin Pi stack.",
        lifespan=lifespan,
        docs_url="/api/docs",
        redoc_url=None,
        openapi_url="/api/openapi.json",
    )

    # -- middleware (order: outermost wraps innermost; declared bottom-up) ---
    if cfg.cors_list:
        app.add_middleware(
            CORSMiddleware,
            allow_origins=cfg.cors_list,
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )
    app.add_middleware(RequestLoggerMiddleware, writer=writer)
    app.add_middleware(BearerAuthMiddleware, token=cfg.auth_token)

    # -- routes -------------------------------------------------------------
    app.include_router(api_v1_router, prefix="/api/v1")
    app.include_router(pages_router)

    if _STATIC_DIR.is_dir():
        app.mount("/static", StaticFiles(directory=_STATIC_DIR), name="static")

    templates = Jinja2Templates(directory=str(_TEMPLATES_DIR))
    templates.env.globals["app_name"] = "Hecquin Dashboard"
    app.state.templates = templates

    @app.get("/health", tags=["system"])
    def health() -> JSONResponse:
        return JSONResponse({"status": "ok", "db": str(cfg.db_path)})

    return app


# ASGI entry point for uvicorn `hecquin_dashboard.main:app`.
app = create_app()


def run() -> None:
    """Console-script entry point: launch uvicorn with SSL when configured."""
    import uvicorn

    cfg = get_settings()
    kwargs: dict = {
        "host": cfg.host,
        "port": cfg.port,
        "log_level": cfg.log_level.lower(),
        "proxy_headers": True,
        "forwarded_allow_ips": "*",
    }
    if cfg.tls_enabled:
        kwargs["ssl_certfile"] = str(cfg.ssl_cert)
        kwargs["ssl_keyfile"] = str(cfg.ssl_key)

    uvicorn.run("hecquin_dashboard.main:app", **kwargs)


if __name__ == "__main__":  # pragma: no cover
    run()
