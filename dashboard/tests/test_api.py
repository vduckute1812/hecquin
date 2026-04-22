"""End-to-end HTTP tests through FastAPI TestClient."""

from __future__ import annotations


def test_health_is_public(client):
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_overview_endpoint_returns_expected_fields(client):
    r = client.get("/api/v1/overview")
    assert r.status_code == 200
    body = r.json()
    for key in (
        "today_api_calls",
        "today_inbound_requests",
        "today_sessions",
        "avg_pron_score_7d",
        "vocab_total",
    ):
        assert key in body
    assert body["vocab_total"] == 5


def test_api_calls_daily_respects_days(client):
    r = client.get("/api/v1/api-calls/daily?days=7")
    assert r.status_code == 200
    body = r.json()
    assert body["outbound_daily"]["type"] == "bar"
    assert body["totals_outbound"] == 4


def test_api_calls_daily_rejects_bad_days(client):
    assert client.get("/api/v1/api-calls/daily?days=0").status_code == 422
    assert client.get("/api/v1/api-calls/daily?days=9999").status_code == 422


def test_learning_overview_endpoint(client):
    r = client.get("/api/v1/learning/overview?days=30")
    assert r.status_code == 200
    body = r.json()
    assert body["vocab_total"] == 5
    assert "vocab_growth" in body
    assert body["vocab_growth"]["type"] == "line"


def test_index_page_renders(client):
    r = client.get("/")
    assert r.status_code == 200
    assert "text/html" in r.headers["content-type"]
    assert "Hecquin" in r.text


def test_request_logger_records_traffic(client, seeded_db):
    # Hit a few endpoints, then verify rows appear in request_logs.
    for _ in range(3):
        client.get("/health")
    client.get("/api/v1/overview")

    import sqlite3
    conn = sqlite3.connect(seeded_db)
    try:
        (cnt,) = conn.execute(
            "SELECT COUNT(*) FROM request_logs WHERE path = '/api/v1/overview';"
        ).fetchone()
    finally:
        conn.close()
    assert cnt >= 1


def test_auth_middleware_blocks_without_token(seeded_db):
    # Build a second app instance that *does* require a token and make sure
    # unauthenticated API calls are rejected while /health stays public.
    from fastapi.testclient import TestClient

    from hecquin_dashboard.config import Settings, get_settings
    from hecquin_dashboard.main import create_app

    cfg = Settings(
        db_path=seeded_db,
        auth_token="secret-token",
        ssl_cert=None,
        ssl_key=None,
        log_level="WARNING",
    )
    get_settings.cache_clear()
    app = create_app(cfg)
    with TestClient(app) as c:
        assert c.get("/health").status_code == 200
        assert c.get("/api/v1/overview").status_code == 401
        ok = c.get(
            "/api/v1/overview",
            headers={"Authorization": "Bearer secret-token"},
        )
        assert ok.status_code == 200
    get_settings.cache_clear()
