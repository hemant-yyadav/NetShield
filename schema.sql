-- Initial schema. Keep this aligned with api/models.py.

CREATE TABLE traffic_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
  domain TEXT NOT NULL,
  method TEXT,
  status_code INTEGER,
  request_size INTEGER,
  response_size INTEGER,
  blocked BOOLEAN DEFAULT FALSE,
  threat_category TEXT,
  client_ip TEXT
);

CREATE TABLE system_stats (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
  stat_key TEXT UNIQUE NOT NULL,
  stat_value REAL
);

CREATE TABLE threats (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  domain TEXT UNIQUE NOT NULL,
  category TEXT DEFAULT 'malware',
  source TEXT DEFAULT 'steven_black',
  added_timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
  is_active BOOLEAN DEFAULT TRUE
);

CREATE TABLE threat_intel_stats (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  last_update_time DATETIME DEFAULT CURRENT_TIMESTAMP,
  last_added_count INTEGER DEFAULT 0,
  total_domains INTEGER DEFAULT 0
);

CREATE TABLE vault_files (
  id TEXT PRIMARY KEY,
  original_filename TEXT NOT NULL,
  encrypted_path TEXT NOT NULL,
  encryption_key TEXT NOT NULL,
  file_hash TEXT,
  download_token TEXT UNIQUE,
  token_expiry DATETIME,
  is_retrieved BOOLEAN DEFAULT FALSE,
  uploaded_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_traffic_timestamp ON traffic_logs(timestamp);
CREATE INDEX idx_traffic_domain ON traffic_logs(domain);
CREATE INDEX idx_traffic_blocked ON traffic_logs(blocked);
CREATE INDEX idx_threats_domain ON threats(domain);
CREATE INDEX idx_vault_download_token ON vault_files(download_token);
