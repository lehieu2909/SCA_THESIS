from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
import base64
import os
import secrets
import sqlite3
from datetime import datetime
from typing import Optional

app = FastAPI(title="Smart Car Access Server")


# Database initialization
def init_db():
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS vehicles (
            vehicle_id TEXT PRIMARY KEY,
            pairing_id TEXT NOT NULL,
            pairing_key TEXT NOT NULL,
            created_at TEXT NOT NULL
        )
    ''')
    conn.commit()
    conn.close()
    print("✓ Database initialized")


# Database operations
def save_vehicle_pairing(vehicle_id: str, pairing_id: str, pairing_key: bytes):
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()

    pairing_key_b64 = base64.b64encode(pairing_key).decode()
    created_at = datetime.utcnow().isoformat()

    cursor.execute('''
        INSERT OR REPLACE INTO vehicles (vehicle_id, pairing_id, pairing_key, created_at)
        VALUES (?, ?, ?, ?)
    ''', (vehicle_id, pairing_id, pairing_key_b64, created_at))

    conn.commit()
    conn.close()


def get_vehicle_pairing(vehicle_id: str) -> Optional[dict]:
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()

    cursor.execute('''
        SELECT pairing_id, pairing_key, created_at
        FROM vehicles
        WHERE vehicle_id = ?
    ''', (vehicle_id,))

    result = cursor.fetchone()
    conn.close()

    if result:
        return {
            "pairing_id": result[0],
            "pairing_key": base64.b64decode(result[1]),
            "created_at": result[2]
        }
    return None


class PairingRequest(BaseModel):
    vehicle_id: str
    vehicle_public_key_b64: str


class PairingResponse(BaseModel):
    pairing_id: str
    server_public_key_b64: str
    encrypted_pairing_key_b64: str
    nonce_b64: str


@app.get("/check-pairing/{vehicle_id}")
def check_pairing_status(vehicle_id: str):
    """Check if a vehicle is already paired"""
    vehicle_data = get_vehicle_pairing(vehicle_id)

    if vehicle_data:
        return {
            "paired": True,
            "vehicle_id": vehicle_id,
            "pairing_id": vehicle_data["pairing_id"],
            "paired_at": vehicle_data["created_at"]
        }
    else:
        return {
            "paired": False,
            "vehicle_id": vehicle_id,
            "message": "Vehicle not paired"
        }


@app.post("/owner-pairing", response_model=PairingResponse)
def owner_pairing(req: PairingRequest):
    """Initial vehicle pairing - establishes and stores pairing key"""

    # --- Load vehicle public key ---
    try:
        vehicle_pub_bytes = base64.b64decode(req.vehicle_public_key_b64)
        vehicle_public_key = serialization.load_der_public_key(vehicle_pub_bytes)
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid public key")

    # --- Server EC key ---
    server_private_key = ec.generate_private_key(ec.SECP256R1())
    server_public_key = server_private_key.public_key()

    # --- ECDH ---
    shared_secret = server_private_key.exchange(ec.ECDH(), vehicle_public_key)

    # --- Derive KEK (Key Encryption Key) ---
    kek = HKDF(
        algorithm=hashes.SHA256(),
        length=16,
        salt=None,
        info=b"owner-pairing-kek",
    ).derive(shared_secret)

    # --- Generate actual pairing key (what vehicle will store) ---
    pairing_key = os.urandom(16)  # 128-bit symmetric key
    pairing_id = secrets.token_hex(8)

    # --- Encrypt pairing key with KEK ---
    aesgcm = AESGCM(kek)
    nonce = os.urandom(12)
    encrypted_pairing_key = aesgcm.encrypt(
        nonce,
        pairing_key,
        None
    )

    # --- Serialize server public key ---
    server_pub_bytes = server_public_key.public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )

    # --- Save to database ---
    save_vehicle_pairing(req.vehicle_id, pairing_id, pairing_key)

    print(f"\n{'=' * 50}")
    print(f"✓ Vehicle Paired Successfully")
    print(f"{'=' * 50}")
    print(f"Vehicle ID:  {req.vehicle_id}")
    print(f"Pairing ID:  {pairing_id}")
    print(f"Pairing Key: {pairing_key.hex()}")
    print(f"{'=' * 50}\n")

    return PairingResponse(
        pairing_id=pairing_id,
        server_public_key_b64=base64.b64encode(server_pub_bytes).decode(),
        encrypted_pairing_key_b64=base64.b64encode(encrypted_pairing_key).decode(),
        nonce_b64=base64.b64encode(nonce).decode()
    )


@app.get("/vehicle/{vehicle_id}")
def get_vehicle_info(vehicle_id: str):
    """Get vehicle pairing information (for testing)"""
    vehicle_data = get_vehicle_pairing(vehicle_id)

    if not vehicle_data:
        raise HTTPException(status_code=404, detail="Vehicle not found")

    return {
        "vehicle_id": vehicle_id,
        "pairing_id": vehicle_data["pairing_id"],
        "pairing_key_hex": vehicle_data["pairing_key"].hex(),
        "created_at": vehicle_data["created_at"]
    }


@app.get("/vehicles")
def list_all_vehicles():
    """List all paired vehicles (for testing)"""
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()

    cursor.execute('SELECT vehicle_id, pairing_id, created_at FROM vehicles')
    vehicles = cursor.fetchall()
    conn.close()

    return {
        "total": len(vehicles),
        "vehicles": [
            {
                "vehicle_id": v[0],
                "pairing_id": v[1],
                "created_at": v[2]
            }
            for v in vehicles
        ]
    }


@app.delete("/vehicle/{vehicle_id}")
def delete_vehicle(vehicle_id: str):
    """Delete vehicle pairing (for testing)"""
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()

    cursor.execute('DELETE FROM vehicles WHERE vehicle_id = ?', (vehicle_id,))
    deleted = cursor.rowcount
    conn.commit()
    conn.close()

    if deleted == 0:
        raise HTTPException(status_code=404, detail="Vehicle not found")

    return {"message": "Vehicle deleted successfully", "vehicle_id": vehicle_id}


@app.get("/")
def root():
    return {
        "message": "Smart Car Access Server",
        "database": "SQLite (car_access.db)",
        "endpoints": {
            "GET /check-pairing/{vehicle_id}": "Check if vehicle is paired",
            "POST /owner-pairing": "Pair vehicle and store key in database",
            "GET /vehicle/{vehicle_id}": "Get vehicle info",
            "GET /vehicles": "List all vehicles",
            "DELETE /vehicle/{vehicle_id}": "Delete vehicle"
        }
    }


# Initialize database on startup
@app.on_event("startup")
async def startup_event():
    init_db()
    print("\n" + "=" * 50)
    print("Smart Car Access Server Started")
    print("=" * 50)
    print("Database: car_access.db (SQLite)")
    print("Pairing keys are now stored persistently!")
    print("\nAvailable Endpoints:")
    print("  GET  /check-pairing/{vehicle_id}")
    print("  POST /owner-pairing")
    print("  GET  /vehicle/{vehicle_id}")
    print("  GET  /vehicles")
    print("  DELETE /vehicle/{vehicle_id}")
    print("=" * 50 + "\n")