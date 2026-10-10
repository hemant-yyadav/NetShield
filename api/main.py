import os
import sqlite3
import asyncio
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from database import engine, DB_PATH
from routers import traffic, vault, threats
from websockets_manager import poll_database_for_updates
import models

def init_db():
    if not os.path.exists(DB_PATH):
        os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
        
        schema_path = None
        if os.path.exists("/app/schema.sql"):
            schema_path = "/app/schema.sql"
        elif os.path.exists("../schema.sql"):
            schema_path = "../schema.sql"
            
        if schema_path:
            with open(schema_path, "r") as f:
                schema_script = f.read()
            conn = sqlite3.connect(DB_PATH)
            conn.executescript(schema_script)
            conn.commit()
            conn.close()
            print(f"Database initialized from {schema_path}")
            
    # Always verify schema (this generates 'threats' if it was missing from the older .db file natively!)
    models.Base.metadata.create_all(bind=engine)
    print("Database partially initialized from SQLAlchemy models")

init_db()

app = FastAPI(title="NetShield API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(traffic.router)
app.include_router(vault.router)
app.include_router(threats.router)

@app.on_event("startup")
async def startup_event():
    asyncio.create_task(poll_database_for_updates())

@app.get("/")
def read_root():
    return {"status": "ok", "service": "NetShield FastAPI Bridge"}
