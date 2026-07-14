import Database from "better-sqlite3";

// Single-file database, lives next to this script.
const db = new Database("fan_data.db");
db.pragma("journal_mode = WAL");

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
