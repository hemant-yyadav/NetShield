from sqlalchemy import Boolean, Column, Integer, String, DateTime, Float
from datetime import datetime
from database import Base

class TrafficLog(Base):
    __tablename__ = "traffic_logs"

    id = Column(Integer, primary_key=True, index=True)
    timestamp = Column(DateTime, default=datetime.utcnow)
    domain = Column(String, index=True)
    method = Column(String)
    status_code = Column(Integer)
    request_size = Column(Integer)
    response_size = Column(Integer)
    blocked = Column(Boolean, default=False)
    threat_category = Column(String, nullable=True)
    client_ip = Column(String, nullable=True)

class SystemStat(Base):
    __tablename__ = "system_stats"
    
    id = Column(Integer, primary_key=True, index=True)
    timestamp = Column(DateTime, default=datetime.utcnow)
    stat_key = Column(String, unique=True, index=True)
    stat_value = Column(Float)

class Threat(Base):
    __tablename__ = "threats"
    
    id = Column(Integer, primary_key=True, index=True)
    domain = Column(String(255), unique=True, index=True, nullable=False)
    category = Column(String(50), default="malware")
    source = Column(String(50), default="steven_black")
    added_timestamp = Column(DateTime, default=datetime.utcnow)
    is_active = Column(Boolean, default=True)

class ThreatIntelligenceStat(Base):
    __tablename__ = "threat_intel_stats"
    
    id = Column(Integer, primary_key=True, index=True)
    last_update_time = Column(DateTime, default=datetime.utcnow)
    last_added_count = Column(Integer, default=0)
    total_domains = Column(Integer, default=0)

class VaultFile(Base):
    __tablename__ = "vault_files"
    
    id = Column(String(255), primary_key=True, index=True)
    original_filename = Column(String(255), nullable=False)
    encrypted_path = Column(String(255), nullable=False)
    encryption_key = Column(String(255), nullable=False)
    file_hash = Column(String(255), nullable=True)
    download_token = Column(String(255), unique=True, index=True, nullable=True)
    token_expiry = Column(DateTime, nullable=True)
    is_retrieved = Column(Boolean, default=False)
    uploaded_at = Column(DateTime, default=datetime.utcnow)
