import sqlite3
import asyncio
import re
from datetime import datetime
from fastapi import APIRouter, BackgroundTasks, HTTPException
from pydantic import BaseModel
from database import DB_PATH
import threat_updater

router = APIRouter(prefix="/api/threats", tags=["threats"])

sync_state = {
    "status": "idle",
    "message": "Standing by.",
    "last_sync": None
}

class CustomDomainRequest(BaseModel):
    domain: str

def bg_sync_threats():
    global sync_state
    sync_state["status"] = "updating"
    sync_state["message"] = "Fetching Intelligence from Global Sources..."
    try:
        threat_updater.update_threats()
        sync_state["status"] = "idle"
        sync_state["message"] = "Update completed successfully"
        sync_state["last_sync"] = datetime.now().isoformat()
    except Exception as e:
        sync_state["status"] = "failed"
        sync_state["message"] = f"Fatal Engine Error: {str(e)}"

@router.post("/refresh")
async def trigger_sync(background_tasks: BackgroundTasks):
    global sync_state
    if sync_state["status"] == "updating":
        raise HTTPException(status_code=400, detail="Lock engaged. Synchronization already executing.")
    background_tasks.add_task(bg_sync_threats)
    return {"message": "Pipeline unlocked. Async execution fired."}

@router.get("/status")
def get_sync_status():
    global sync_state
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    # Pull dynamic metrics securely from system
    stat_row = cursor.execute("SELECT last_update_time, last_added_count FROM threat_intel_stats ORDER BY id DESC LIMIT 1").fetchone()
    conn.close()
    
    return {
        "status": sync_state["status"],
        "message": sync_state["message"],
        "last_sync": stat_row[0] if stat_row else sync_state["last_sync"],
        "last_added_count": stat_row[1] if stat_row else 0
    }

@router.get("/stats")
def get_threat_stats():
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        total_threats = cursor.execute("SELECT COUNT(*) FROM threats").fetchone()[0]
        
        # Source aggregations dynamically fetched
        sources = dict(cursor.execute("SELECT source, COUNT(*) FROM threats GROUP BY source").fetchall())
        
        stat_row = cursor.execute("SELECT last_update_time, last_added_count FROM threat_intel_stats ORDER BY id DESC LIMIT 1").fetchone()
        
        conn.close()
        
        return {
            "total_domains": total_threats,
            "sources": sources,
            "last_updated": stat_row[0] if stat_row else None,
            "last_added_count": stat_row[1] if stat_row else 0
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/recent")
def get_recent_threats(limit: int = 10):
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        recent = cursor.execute("SELECT domain, category, source, added_timestamp FROM threats ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
        conn.close()
        return [{"domain": r[0], "category": r[1], "source": r[2], "added_timestamp": r[3]} for r in recent]
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/check")
def check_domain(domain: str):
    domain = domain.strip().lower()
    
    # Secure RegEx validation native check
    if not domain or not re.match(r"^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$", domain):
        raise HTTPException(status_code=400, detail="Invalid domain format")
        
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        result = cursor.execute("SELECT category, source FROM threats WHERE domain = ?", (domain,)).fetchone()
        conn.close()
        
        if result:
            return {"domain": domain, "blocked": True, "category": result[0], "source": result[1]}
        return {"domain": domain, "blocked": False}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/add")
def add_custom_threat(req: CustomDomainRequest):
    domain = req.domain.strip().lower()
    if not domain or not re.match(r"^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z0-9][a-z0-9-]{0,61}[a-z0-9]$", domain):
        raise HTTPException(status_code=400, detail="Invalid domain format.")
        
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        # Verify it doesn't already exist
        exists = cursor.execute("SELECT 1 FROM threats WHERE domain = ?", (domain,)).fetchone()
        if exists:
            conn.close()
            raise HTTPException(status_code=400, detail="Domain already blocked.")
            
        cursor.execute(
            "INSERT INTO threats (domain, category, source, added_timestamp, is_active) VALUES (?, ?, ?, ?, ?)",
            (domain, 'custom', 'custom', datetime.now().isoformat(), 1)
        )
        conn.commit()
        conn.close()
        return {"domain": domain, "message": "Domain directly mapped to Proxy Blocklist organically."}
    except Exception as e:
        if isinstance(e, HTTPException): raise e
        raise HTTPException(status_code=500, detail=str(e))
