from fastapi import APIRouter, Depends, Query, WebSocket, WebSocketDisconnect
from sqlalchemy.orm import Session
from typing import List
import models, schemas
from database import get_db
from websockets_manager import manager

router = APIRouter(
    prefix="/api/traffic",
    tags=["traffic"]
)

@router.get("/logs", response_model=List[schemas.TrafficLogResponse])
def get_traffic_logs(
    skip: int = 0, 
    limit: int = Query(default=100, le=1000), 
    db: Session = Depends(get_db)):
    
    logs = db.query(models.TrafficLog).order_by(models.TrafficLog.timestamp.desc()).offset(skip).limit(limit).all()
    return logs

@router.get("/stats")
def get_traffic_stats(db: Session = Depends(get_db)):
    total_requests = db.query(models.TrafficLog).count()
    blocked_requests = db.query(models.TrafficLog).filter(models.TrafficLog.blocked == True).count()
    
    stats = {}
    for stat in db.query(models.SystemStat).all():
        stats[stat.stat_key] = stat.stat_value
        
    return {
        "total_requests": total_requests,
        "blocked_requests": blocked_requests,
        "allowed_requests": total_requests - blocked_requests,
        "system_stats": stats
    }

import datetime

@router.get("/chart")
def get_traffic_chart(db: Session = Depends(get_db)):
    try:
        twenty_four_hours_ago = datetime.datetime.utcnow() - datetime.timedelta(hours=24)
        logs = db.query(models.TrafficLog).filter(models.TrafficLog.timestamp >= twenty_four_hours_ago).all()
        
        buckets = {}
        for i in range(24):
            h = (datetime.datetime.utcnow() - datetime.timedelta(hours=23 - i)).strftime("%H:00")
            buckets[h] = {"time": h, "allowed": 0, "blocked": 0}
            
        for log in logs:
            key = log.timestamp.strftime("%H:00")
            if key in buckets:
                if log.blocked:
                    buckets[key]["blocked"] += 1
                else:
                    buckets[key]["allowed"] += 1
                    
        return list(buckets.values())
    except Exception as e:
        return []

@router.websocket("/live")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            # Just keep the connection alive
            data = await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)
