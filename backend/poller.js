import db from "./db.js";

const ESP32_URL = "http://192.168.4.1/";

// Storing once a minute is plenty for daily/weekly history charts.
// Live values on the dashboard still come straight from the ESP32 (see /api/live).
const POLL_INTERVAL_MS = 60_000;

const insertReading = db.prepare(`
  INSERT INTO readings (timestamp, temperature, humidity, pwm, fan_speed_pct)
  VALUES (@timestamp, @temperature, @humidity, @pwm, @fan_speed_pct)
`);

async function pollOnce() {
  try {
    const res = await fetch(ESP32_URL, { signal: AbortSignal.timeout(3000) });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    if (!data.dht_ok) {
      console.warn("[poller] skipped — DHT11 read was invalid on the ESP32");
      return;
    }

    insertReading.run({
      timestamp: Date.now(),
      temperature: data.celsius,      // firmware reports celsius, not "temperature"
      humidity: data.humidity,
      pwm: data.pwm,
      fan_speed_pct: data.speed,      // firmware sends a 0-100 int, not a "50%" label
    });
  } catch (err) {
    console.error("[poller] ESP32 unreachable:", err.message);
  }
}

export function startPolling() {
  pollOnce(); // run immediately, then on interval
  setInterval(pollOnce, POLL_INTERVAL_MS);
}
