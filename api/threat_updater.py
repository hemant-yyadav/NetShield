import os
import sqlite3
import requests
from datetime import datetime

DB_PATH = os.getenv("DB_PATH", "/data/netshield.db")
if not os.path.exists(DB_PATH):
    if os.path.exists("../netshield.db"): DB_PATH = "../netshield.db"
    elif os.path.exists("./netshield.db"): DB_PATH = "./netshield.db"

STEVENBLACK_HOSTS_URL = "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts"
URLHAUS_URL = "https://urlhaus.abuse.ch/downloads/hostfile/"

def update_threats():
    domains_to_insert = []
    
    # 1. Fetch StevenBlack
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Fetching StevenBlack root blocklists...")
    try:
        response = requests.get(STEVENBLACK_HOSTS_URL, timeout=15)
        response.raise_for_status()
        for line in response.text.splitlines():
            line = line.strip()
            if not line or line.startswith('#'): continue
            parts = line.split()
            if len(parts) >= 2 and parts[0] in ('0.0.0.0', '127.0.0.1'):
                domain = parts[1]
                if domain not in ('localhost', 'localhost.localdomain', 'local', 'broadcasthost', '0.0.0.0'):
                    domains_to_insert.append((domain, 'ads', 'steven_black', datetime.now().isoformat(), 1))
    except Exception as e:
        print(f"StevenBlack fetch failed: {e}")

    # 2. Fetch URLHaus
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Fetching URLHaus malware node lists...")
    try:
        response = requests.get(URLHAUS_URL, timeout=15)
        response.raise_for_status()
        for line in response.text.splitlines():
            line = line.strip()
            if not line or line.startswith('#'): continue
            parts = line.split()
            if len(parts) >= 2 and parts[0] in ('127.0.0.1',):
                domain = parts[1]
                if domain != 'localhost':
                    domains_to_insert.append((domain, 'malware', 'urlhaus', datetime.now().isoformat(), 1))
    except Exception as e:
        print(f"URLHaus fetch failed: {e}")

    added_count = len(domains_to_insert)
    if not added_count:
        print("No domains harvested.")
        return

    # Database Hook
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Mapping {added_count} nodes into Proxy Engine SQLite ({DB_PATH})...")
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        
        cursor.executemany(
            "INSERT OR IGNORE INTO threats (domain, category, source, added_timestamp, is_active) VALUES (?, ?, ?, ?, ?)",
            domains_to_insert
        )
        
        # Calculate strict total for telemetry
        total_domains = cursor.execute("SELECT COUNT(*) FROM threats").fetchone()[0]
        
        # Log into ThreatIntelligenceStats securely
        cursor.execute("""
            INSERT INTO threat_intel_stats (last_update_time, last_added_count, total_domains) 
            VALUES (?, ?, ?)
        """, (datetime.utcnow(), added_count, total_domains))
        
        conn.commit()
        conn.close()
        print(f"[{datetime.now().strftime('%H:%M:%S')}] NetShield Successfully patched {added_count} total domains into active Engine!")
    except Exception as e:
        print(f"Database bridging failed natively: {e}")

if __name__ == "__main__":
    update_threats()
