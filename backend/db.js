import { DatabaseSync } from "node:sqlite";

// Single-file database, lives next to this script.
// Uses Node's built-in SQLite (no native compilation, no extra dependency —
// requires Node.js 22.5+; unflagged/no CLI flag needed on Node 23.4+/24+).
const db = new DatabaseSync("fan_data.db");

db.exec(`
  CREATE TABLE IF NOT EXISTS readings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp   INTEGER NOT NULL,   -- epoch ms
    temperature REAL,
    humidity    REAL,
    pwm         INTEGER,
    fan_speed_pct INTEGER
  );

  CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON readings (timestamp);
`);

export default db;