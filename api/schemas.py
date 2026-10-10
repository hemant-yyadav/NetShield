from pydantic import BaseModel
from typing import Optional
from datetime import datetime

class TrafficLogBase(BaseModel):
    domain: str
    method: Optional[str] = None
    status_code: Optional[int] = None
    request_size: Optional[int] = None
    response_size: Optional[int] = None
    blocked: bool = False
    threat_category: Optional[str] = None
    client_ip: Optional[str] = None

class TrafficLogCreate(TrafficLogBase):
    pass

class TrafficLogResponse(TrafficLogBase):
    id: int
    timestamp: datetime

    class Config:
        from_attributes = True

class SystemStatBase(BaseModel):
    stat_key: str
    stat_value: str

class SystemStatResponse(SystemStatBase):
    id: int
    updated_at: datetime
    
    class Config:
        from_attributes = True
