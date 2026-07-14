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
