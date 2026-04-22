"""Typed settings loaded from env / .env via pydantic-settings.

A single `Settings` instance is created lazily by `get_settings()` so tests
can clear the cache and override env without touching module-level state.
"""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Application configuration.

    All variables use the ``HECQUIN_`` prefix so they do not collide with
    unrelated environment state on the host.
    """

    model_config = SettingsConfigDict(
        env_prefix="HECQUIN_",
        env_file=(".env",),
        env_file_encoding="utf-8",
        extra="ignore",
    )

    db_path: Path = Field(
        default=Path("./hecquin.db"),
        description="SQLite file written by C++ LearningStore.",
    )

    host: str = Field(default="0.0.0.0")
    port: int = Field(default=443, ge=1, le=65535)

    ssl_cert: Path | None = Field(default=None)
    ssl_key: Path | None = Field(default=None)

    auth_token: str = Field(default="", description="Bearer token; empty = no auth.")

    cors_origins: str = Field(default="", description="Comma-separated origins.")

    log_level: str = Field(default="INFO")
    default_days: int = Field(default=30, ge=1, le=3650)

    @field_validator("ssl_cert", "ssl_key", mode="before")
    @classmethod
    def _empty_to_none(cls, v):
        if v in ("", None):
            return None
        return v

    @property
    def tls_enabled(self) -> bool:
        return self.ssl_cert is not None and self.ssl_key is not None

    @property
    def cors_list(self) -> list[str]:
        return [o.strip() for o in self.cors_origins.split(",") if o.strip()]


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    """Return a cached Settings instance. Tests should call `get_settings.cache_clear()`."""
    return Settings()
