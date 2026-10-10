import os
import json
import uuid
import subprocess
from datetime import datetime, timedelta
from typing import List, Optional
from fastapi import APIRouter, File, UploadFile, Depends, HTTPException, BackgroundTasks, Form
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session
from database import SessionLocal
import models
from pydantic import BaseModel

router = APIRouter(prefix="/api/vault", tags=["vault"])

VAULT_DIR = os.getenv("VAULT_PATH", "/vault")
ENCRYPTOR_BIN = "/app/encryptor/encryptor"
DEFAULT_EXPIRY_HOURS = int(os.getenv("VAULT_EXPIRY_HOURS", "1"))

# Server-level isolation keys must be configured explicitly in production.
SERVER_SECRET = os.getenv("SERVER_SECRET")
MASTER_PASSWORD = os.getenv("SERVER_MASTER_PASSWORD")

if not SERVER_SECRET or len(SERVER_SECRET) < 32:
    raise RuntimeError("SERVER_SECRET must be set and contain at least 32 characters.")
if not MASTER_PASSWORD or len(MASTER_PASSWORD) < 16:
    raise RuntimeError("SERVER_MASTER_PASSWORD must be set and contain at least 16 characters.")

os.makedirs(VAULT_DIR, exist_ok=True)

# Dependency
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

class VaultFileResponse(BaseModel):
    id: str
    original_filename: str
    uploaded_at: datetime
    is_retrieved: bool
    download_token: Optional[str]
    token_expiry: Optional[datetime]

@router.post("/upload")
async def upload_file(
    file: UploadFile = File(...), 
    password: str = Depends(lambda p=Form(...): p[1] if isinstance(p, tuple) else p),
    db: Session = Depends(get_db)
):
    # Personal Vault Manual Upload (requires personal secure password)
    file_id = str(uuid.uuid4())
    temp_path = os.path.join(VAULT_DIR, f"temp_{file_id}")
    enc_path = os.path.join(VAULT_DIR, f"{file_id}.enc")

    with open(temp_path, "wb") as f:
        f.write(await file.read())

    try:
        result = subprocess.run(
            [ENCRYPTOR_BIN, "encrypt", temp_path, enc_path, password],
            capture_output=True, text=True, check=False
        )
        if result.returncode != 0:
            raise Exception("Encryption subprocess failed natively.")
        output = json.loads(result.stdout)
        salt_hex = output["salt"]
        file_hash = output.get("hash", "")
    except Exception as e:
        if os.path.exists(temp_path): os.remove(temp_path)
        raise HTTPException(status_code=500, detail=str(e))

    os.remove(temp_path)

    # Save to SQLite directly into Personal Vault scope
    db_file = models.VaultFile(
        id=file_id,
        original_filename=file.filename,
        encrypted_path=enc_path,
        encryption_key=salt_hex, 
        file_hash=file_hash,
        download_token="personal_vault_item",
        token_expiry=None # No expiry for explicitly stored files
    )
    db.add(db_file)
    db.commit()

    return {"file_id": file_id, "status": "Securely Encrypted"}


@router.post("/share/generate")
async def share_generate(
    file: UploadFile = File(...),
    expiry_hours: int = Depends(lambda e=Form(24): int(e[1]) if isinstance(e, tuple) else int(e)),
    db: Session = Depends(get_db)
):
    # System Isolated Share Mode (Uses Server Master Password)
    file_id = str(uuid.uuid4())
    temp_path = os.path.join(VAULT_DIR, f"temp_{file_id}")
    enc_path = os.path.join(VAULT_DIR, f"share_{file_id}.enc")

    with open(temp_path, "wb") as f:
        f.write(await file.read())

    try:
        result = subprocess.run(
            [ENCRYPTOR_BIN, "encrypt", temp_path, enc_path, MASTER_PASSWORD],
            capture_output=True, text=True, check=False
        )
        if result.returncode != 0: raise Exception("Shared encryption failed natively.")
        output = json.loads(result.stdout)
        salt_hex = output["salt"]
    except Exception as e:
        if os.path.exists(temp_path): os.remove(temp_path)
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        if os.path.exists(temp_path): os.remove(temp_path)

    # Generate Native C++ JWT mapped precisely to target hours
    try:
        jwt_res = subprocess.run(
            [ENCRYPTOR_BIN, "sign_token", file_id, str(expiry_hours), "1", SERVER_SECRET],
            capture_output=True, text=True, check=False
        )
        jwt_output = json.loads(jwt_res.stdout)
        jwt_token = jwt_output["token"]
    except Exception as e:
        raise HTTPException(status_code=500, detail="JWT Generation failed natively.")

    db_file = models.VaultFile(
        id=file_id,
        original_filename=file.filename,
        encrypted_path=enc_path,
        encryption_key=salt_hex,
        file_hash="",
        download_token=jwt_token, 
        token_expiry=datetime.utcnow() + timedelta(hours=int(expiry_hours))
    )
    db.add(db_file)
    db.commit()

    return {
        "share_url": f"/api/vault/share/download/{jwt_token}",
        "jwt": jwt_token
    }


@router.get("/share/download/{token}")
def share_download(token: str, db: Session = Depends(get_db)):
    # 1. Verify JWT Cryptographically via C++ validation hook
    try:
        verify_res = subprocess.run(
            [ENCRYPTOR_BIN, "verify_token", token, SERVER_SECRET],
            capture_output=True, text=True, check=False
        )
        if verify_res.returncode != 0:
            err_out = json.loads(verify_res.stdout) if verify_res.stdout else {}
            err = err_out.get("error", "JWT Validation Integrity Failed")
            raise HTTPException(status_code=403, detail=err)
        
        verify_obj = json.loads(verify_res.stdout)
        file_id = verify_obj["file_id"]
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail="JWT verification engine fault")

    # 2. Database validation layer (strictly for One-Time Link burn)
    db_file = db.query(models.VaultFile).filter(models.VaultFile.id == file_id).first()
    if not db_file:
         raise HTTPException(status_code=404, detail="File deleted or missing.")
         
    if db_file.is_retrieved:
         raise HTTPException(status_code=403, detail="File already retrieved natively (One Time Constraint Validated).")

    db_file.is_retrieved = True
    db.commit()

    temp_dec_path = os.path.join(VAULT_DIR, f"dec_{file_id}_{db_file.original_filename}")

    # 3. Decrypt on-the-fly dynamically using MASTER_PASSWORD
    try:
        result = subprocess.run(
            [ENCRYPTOR_BIN, "decrypt", db_file.encrypted_path, temp_dec_path, MASTER_PASSWORD],
            capture_output=True, text=True, check=False
        )
        if result.returncode != 0:
             raise Exception("Master decryption tag failed.")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Engine failure: {str(e)}")

    return FileResponse(path=temp_dec_path, filename=db_file.original_filename)


@router.get("/files", response_model=List[VaultFileResponse])
def list_files(db: Session = Depends(get_db)):
    files = db.query(models.VaultFile).order_by(models.VaultFile.uploaded_at.desc()).all()
    return files


@router.post("/decrypt")
def decrypt_manual_file(
    file_id: str = Form(...),
    password: str = Form(...),
    db: Session = Depends(get_db)
):
    db_file = db.query(models.VaultFile).filter(models.VaultFile.id == file_id).first()
    if not db_file:
        raise HTTPException(status_code=404, detail="File not found in Vault.")

    temp_dec_path = os.path.join(VAULT_DIR, f"dec_{db_file.id}_{db_file.original_filename}")

    try:
        result = subprocess.run(
            [ENCRYPTOR_BIN, "decrypt", db_file.encrypted_path, temp_dec_path, password],
            capture_output=True, text=True, check=False
        )
        if result.returncode != 0:
             raise Exception("Authentication Failed. Invalid Password or Data Corrupt.")
             
        output = json.loads(result.stdout)
        if output.get("status") != "success":
             raise Exception("Invalid Password. GCM tag verification failed.")
    except Exception as e:
        if os.path.exists(temp_dec_path): os.remove(temp_dec_path)
        raise HTTPException(status_code=403, detail=f"Authentication Failure: {str(e)}")

    return FileResponse(path=temp_dec_path, filename=db_file.original_filename)
