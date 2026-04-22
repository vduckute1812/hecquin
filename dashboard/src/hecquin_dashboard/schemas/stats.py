"""Aggregated DTOs returned by the stats service."""

from __future__ import annotations

from pydantic import BaseModel

from .chart import ChartJsPayload


class Overview(BaseModel):
    today_api_calls: int
    today_inbound_requests: int
    today_sessions: int
    avg_pron_score_7d: float
    vocab_total: int


class EndpointUsage(BaseModel):
    endpoint: str
    provider: str
    count: int
    avg_latency_ms: float
    error_rate: float


class ApiUsageOverview(BaseModel):
    outbound_daily: ChartJsPayload
    inbound_daily: ChartJsPayload
    top_endpoints: list[EndpointUsage]
    totals_outbound: int
    totals_inbound: int


class LearningOverview(BaseModel):
    vocab_growth: ChartJsPayload
    sessions_daily: ChartJsPayload
    pronunciation_daily: ChartJsPayload
    vocab_total: int


class PhonemeWeakness(BaseModel):
    ipa: str
    attempts: int
    avg_score: float


class TopVocab(BaseModel):
    word: str
    seen_count: int
    mastery: int


class RecentApiCall(BaseModel):
    ts: int
    provider: str
    endpoint: str
    status: int
    latency_ms: int
    ok: bool
    error: str | None


class RecentRequest(BaseModel):
    ts: int
    path: str
    method: str
    status: int
    latency_ms: int
    remote_ip: str | None
