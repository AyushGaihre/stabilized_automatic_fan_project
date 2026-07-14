import express from "express";
import cors from "cors";
import db from "./db.js";
import { startPolling } from "./poller.js";

const ESP32_URL = "http://192.168.4.1";
const PORT = 3001;

const app = express();
app.use(cors());
app.use(express.json());

// ── Live passthrough (kept separate from history so the dashboard's
//    current-reading card stays as low-latency as before) ──────────
app.get("/api/live", async (_req, res) => {
  try {
    const r = await fetch(`${ESP32_URL}/`, { signal: AbortSignal.timeout(3000) });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    res.json(await r.json());
  } catch (err) {
    res.status(502).json({ error: "ESP32 unreachable", detail: err.message });
  }
});

// ── Fan speed control passthrough ───────────────────────────────────
app.get("/api/fan", async (req, res) => {
  const { speed } = req.query;
  if (speed === undefined) return res.status(400).json({ error: "missing speed" });
  try {
    const r = await fetch(`${ESP32_URL}/fan?speed=${encodeURIComponent(speed)}`, {
      signal: AbortSignal.timeout(3000),
    });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    res.json({ ok: true });
  } catch (err) {
    res.status(502).json({ error: "ESP32 unreachable", detail: err.message });
  }
});

// ── History: raw readings, most recent N points (no averaging) ─────
// Lets you see data land minute-by-minute instead of waiting for a full
// hour to fill an "hourly average" bucket in /api/history/today.
app.get("/api/history/recent", (req, res) => {
  const limit = Math.min(parseInt(req.query.limit, 10) || 60, 500);
  const rows = db
    .prepare(
      `
      SELECT
        strftime('%H:%M', timestamp / 1000, 'unixepoch', 'localtime') AS label,
        temperature,
        humidity,
        pwm,
        fan_speed_pct
      FROM readings
      ORDER BY timestamp DESC
      LIMIT ?
      `
    )
    .all(limit);
  res.json(rows.reverse()); // chronological order for the chart
});

// ── History: today's readings, one averaged point per hour ─────────
app.get("/api/history/today", (_req, res) => {
  const startOfToday = new Date();
  startOfToday.setHours(0, 0, 0, 0);
  const since = startOfToday.getTime();

  const rows = db
    .prepare(
      `
      SELECT
        strftime('%H:00', timestamp / 1000, 'unixepoch', 'localtime') AS label,
        ROUND(AVG(temperature), 1) AS temperature,
        ROUND(AVG(humidity), 1)    AS humidity,
        ROUND(AVG(pwm))            AS pwm,
        ROUND(AVG(fan_speed_pct))  AS fan_speed_pct
      FROM readings
      WHERE timestamp >= ?
      GROUP BY label
      ORDER BY label ASC
      `
    )
    .all(since);
  res.json(rows);
});

// ── History: one averaged point per day, last 7 days ───────────────
app.get("/api/history/daily", (_req, res) => {
  const since = Date.now() - 7 * 24 * 60 * 60 * 1000;
  const rows = db
    .prepare(
      `
      SELECT
        date(timestamp / 1000, 'unixepoch', 'localtime') AS label,
        ROUND(AVG(temperature), 1) AS temperature,
        ROUND(AVG(humidity), 1)    AS humidity,
        ROUND(AVG(pwm))            AS pwm,
        ROUND(AVG(fan_speed_pct))  AS fan_speed_pct
      FROM readings
      WHERE timestamp >= ?
      GROUP BY label
      ORDER BY label ASC
      `
    )
    .all(since);
  res.json(rows);
});

// ── History: one averaged point per ISO week, last 4 weeks ─────────
app.get("/api/history/weekly", (_req, res) => {
  const since = Date.now() - 28 * 24 * 60 * 60 * 1000;
  const rows = db
    .prepare(
      `
      SELECT
        strftime('%Y-W%W', timestamp / 1000, 'unixepoch', 'localtime') AS label,
        ROUND(AVG(temperature), 1) AS temperature,
        ROUND(AVG(humidity), 1)    AS humidity,
        ROUND(AVG(pwm))            AS pwm,
        ROUND(AVG(fan_speed_pct))  AS fan_speed_pct
      FROM readings
      WHERE timestamp >= ?
      GROUP BY label
      ORDER BY label ASC
      `
    )
    .all(since);
  res.json(rows);
});

app.listen(PORT, () => {
  console.log(`Fan controller backend running at http://localhost:${PORT}`);
  startPolling();
});