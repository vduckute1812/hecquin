/*
 * Dashboard glue: find every <canvas data-endpoint="..." data-key="..."> and
 * every <table class="data-table" data-endpoint="..." data-key="...">, fetch
 * the endpoint once, and render.
 *
 *   data-key = "__list__"  →  response is the array itself (for tables)
 *   data-key = "<field>"   →  response is an object; render response[field]
 *
 * For charts the payload shape is the Chart.js-compatible object built by
 * services/chart_builder.py: {type, labels, datasets, options}.
 */
(function () {
  "use strict";

  const byEndpoint = new Map(); // endpoint → Promise<json>

  function fetchOnce(endpoint) {
    if (!byEndpoint.has(endpoint)) {
      byEndpoint.set(endpoint, fetch(endpoint, { credentials: "same-origin" })
        .then((r) => {
          if (!r.ok) throw new Error(`${endpoint} → ${r.status}`);
          return r.json();
        }));
    }
    return byEndpoint.get(endpoint);
  }

  function resetCaches() {
    byEndpoint.clear();
  }

  function renderChart(canvas, payload) {
    const existing = Chart.getChart(canvas);
    if (existing) existing.destroy();
    new Chart(canvas.getContext("2d"), {
      type: payload.type,
      data: { labels: payload.labels, datasets: payload.datasets },
      options: Object.assign(
        { maintainAspectRatio: false },
        payload.options || {},
      ),
    });
  }

  function fmtTs(sec) {
    try {
      return new Date(sec * 1000).toISOString().replace("T", " ").slice(0, 19);
    } catch (_) { return String(sec); }
  }

  function renderTable(table, rows) {
    const tbody = table.querySelector("tbody");
    tbody.innerHTML = "";
    if (!rows || rows.length === 0) {
      const tr = document.createElement("tr");
      const td = document.createElement("td");
      td.colSpan = table.querySelectorAll("thead th").length;
      td.textContent = "No data yet.";
      td.style.color = "var(--text-muted)";
      td.style.textAlign = "center";
      tr.appendChild(td);
      tbody.appendChild(tr);
      return;
    }
    for (const row of rows) {
      const tr = document.createElement("tr");
      for (const cell of tableCellsFor(table.id, row)) {
        const td = document.createElement("td");
        td.textContent = cell == null ? "" : String(cell);
        tr.appendChild(td);
      }
      tbody.appendChild(tr);
    }
  }

  function tableCellsFor(tableId, row) {
    switch (tableId) {
      case "topEndpointsTable":
        return [
          row.endpoint, row.provider, row.count,
          row.avg_latency_ms, (row.error_rate * 100).toFixed(1) + "%",
        ];
      case "recentOutboundTable":
        return [
          fmtTs(row.ts), row.provider, row.endpoint,
          row.status, row.latency_ms, row.error || "",
        ];
      case "topVocabTable":
        return [row.word, row.seen_count, row.mastery];
      default:
        return Object.values(row);
    }
  }

  function currentDays() {
    const el = document.getElementById("days");
    const v = el ? parseInt(el.value, 10) : NaN;
    return Number.isFinite(v) && v > 0 ? v : null;
  }

  function withDays(endpoint, days) {
    if (days == null || /[?&]days=/.test(endpoint)) return endpoint;
    const sep = endpoint.includes("?") ? "&" : "?";
    return `${endpoint}${sep}days=${days}`;
  }

  async function refreshAll() {
    resetCaches();
    const days = currentDays();

    for (const canvas of document.querySelectorAll("canvas[data-endpoint]")) {
      const endpoint = withDays(canvas.dataset.endpoint, days);
      const key = canvas.dataset.key;
      try {
        const data = await fetchOnce(endpoint);
        const payload = key === "__list__" ? data : data[key];
        if (payload) renderChart(canvas, payload);
      } catch (err) {
        console.error("chart", canvas.id, err);
      }
    }

    for (const table of document.querySelectorAll("table.data-table[data-endpoint]")) {
      const endpoint = withDays(table.dataset.endpoint, days);
      const key = table.dataset.key;
      try {
        const data = await fetchOnce(endpoint);
        const rows = key === "__list__" ? data : data[key];
        renderTable(table, rows || []);
      } catch (err) {
        console.error("table", table.id, err);
      }
    }

    const stamp = document.getElementById("lastUpdated");
    if (stamp) stamp.textContent = "Last updated " + new Date().toLocaleTimeString();
  }

  document.addEventListener("DOMContentLoaded", () => {
    refreshAll();

    const form = document.getElementById("rangeForm");
    if (form) {
      form.addEventListener("submit", (ev) => {
        ev.preventDefault();
        refreshAll();
      });
    }

    setInterval(refreshAll, 60_000);
  });
})();
