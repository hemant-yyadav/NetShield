import asyncio
import json
from fastapi import WebSocket
from sqlalchemy.orm import Session
from database import SessionLocal
import models
import schemas
from typing import List

class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []
        self.last_seen_id = 0

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast_log(self, parsed_log: dict):
        for connection in self.active_connections:
            await connection.send_text(json.dumps({
                "type": "new_log",
                "data": parsed_log
            }))

    async def broadcast_stats(self, stats: dict):
        for connection in self.active_connections:
            await connection.send_text(json.dumps({
                "type": "update_stats",
                "data": stats
            }))

manager = ConnectionManager()

async def poll_database_for_updates():
    """Background task to poll SQLite for new traffic_logs"""
    db = SessionLocal()
    
    # Initialize last_seen_id to the max current ID so we don't flood on restart
    max_id_log = db.query(models.TrafficLog).order_by(models.TrafficLog.id.desc()).first()
    if max_id_log:
        manager.last_seen_id = max_id_log.id
    else:
        manager.last_seen_id = 0

    while True:
        try:
            # Check for new logs
            new_logs = db.query(models.TrafficLog).filter(models.TrafficLog.id > manager.last_seen_id).all()
            if new_logs:
                for log in new_logs:
                    log_dict = {
                        "id": log.id,
                        "timestamp": log.timestamp.isoformat() if log.timestamp else __import__('datetime').datetime.utcnow().isoformat(),
                        "domain": log.domain,
                        "method": log.method,
                        "status_code": log.status_code,
                        "request_size": log.request_size,
                        "response_size": log.response_size,
                        "blocked": log.blocked,
                        "threat_category": log.threat_category,
                        "client_ip": log.client_ip
                    }
                    await manager.broadcast_log(log_dict)
                    manager.last_seen_id = max(manager.last_seen_id, log.id)

                # If new logs appeared, stats also likely changed
                total_requests = db.query(models.TrafficLog).count()
                blocked_requests = db.query(models.TrafficLog).filter(models.TrafficLog.blocked == True).count()
                
                stats_out = {
                    "total_requests": total_requests,
                    "blocked_requests": blocked_requests,
                    "allowed_requests": total_requests - blocked_requests,
                    "system_stats": {}
                }
                await manager.broadcast_stats(stats_out)
                
        except Exception as e:
            print(f"Error in polling loop: {e}")
            
        # Give DB connection freshness by cleanly ending the transaction loop
        db.rollback() 
        await asyncio.sleep(1)
