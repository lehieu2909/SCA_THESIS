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
import socket
from datetime import datetime, timedelta
from typing import Optional
from contextlib import asynccontextmanager
from zeroconf import ServiceInfo, Zeroconf

# --- Server signing key (ECDSA SECP256R1) for signing friend key bundles ---
SERVER_SIGNING_KEY_PATH = "server_signing_key.pem"
_server_signing_key = None


def get_server_signing_key():
    global _server_signing_key
    if _server_signing_key is None:
        if os.path.exists(SERVER_SIGNING_KEY_PATH):
            with open(SERVER_SIGNING_KEY_PATH, "rb") as f:
                _server_signing_key = serialization.load_pem_private_key(f.read(), password=None)
        else:
            _server_signing_key = ec.generate_private_key(ec.SECP256R1())
            with open(SERVER_SIGNING_KEY_PATH, "wb") as f:
                f.write(_server_signing_key.private_bytes(
                    serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8,
                    serialization.NoEncryption()
                ))
            print("✓ New server signing key generated")
    return _server_signing_key


def _friend_bundle_message(friend_id: str, vehicle_id: str, friend_key_hex: str, expires_at: str) -> bytes:
    return f"{friend_id}:{vehicle_id}:{friend_key_hex}:{expires_at}".encode()


def sign_friend_bundle(friend_id: str, vehicle_id: str, friend_key_hex: str, expires_at: str) -> bytes:
    msg = _friend_bundle_message(friend_id, vehicle_id, friend_key_hex, expires_at)
    return get_server_signing_key().sign(msg, ec.ECDSA(hashes.SHA256()))


def verify_friend_bundle_sig(friend_id: str, vehicle_id: str, friend_key_hex: str, expires_at: str, sig: bytes) -> bool:
    msg = _friend_bundle_message(friend_id, vehicle_id, friend_key_hex, expires_at)
    try:
        get_server_signing_key().public_key().verify(sig, msg, ec.ECDSA(hashes.SHA256()))
        return True
    except Exception:
        return False


def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


@asynccontextmanager
async def lifespan(app: FastAPI):
    import asyncio

    # Startup
    init_db()
    get_server_signing_key()  # Initialize/load signing key at startup
    print("\n" + "=" * 50)
    print("Smart Car Access Server Started")
    print("=" * 50)
    print("Database: car_access.db (SQLite)")
    print("Pairing keys are now stored persistently!")
    print("\nAvailable Endpoints:")
    print("  POST /secure-check-pairing (ENCRYPTED)")
    print("  GET  /check-pairing/{vehicle_id} (PLAIN)")
    print("  POST /owner-pairing")
    print("  GET  /vehicle/{vehicle_id}")
    print("  GET  /vehicles")
    print("  DELETE /vehicle/{vehicle_id}")
    print("  --- Friend Sharing ---")
    print("  POST   /friend-sharing/create")
    print("  GET    /friend-sharing/claim/{claim_token}")
    print("  POST   /validate-friend-key")
    print("  DELETE /friend-sharing/{friend_id}")
    print("  GET    /friend-sharing/list/{vehicle_id}")
    print("  GET    /server-public-key")
    print("=" * 50)

    # Đăng ký mDNS trong thread riêng để không block async event loop
    local_ip = get_local_ip()
    zeroconf = Zeroconf()
    mdns_info = ServiceInfo(
        "_http._tcp.local.",
        "smartcar._http._tcp.local.",
        addresses=[socket.inet_aton(local_ip)],
        port=8000,
        properties={"service": "smartcar"},
    )
    loop = asyncio.get_event_loop()
    try:
        await loop.run_in_executor(None, zeroconf.register_service, mdns_info)
        print(f"mDNS registered: smartcar._http._tcp.local -> {local_ip}:8000")
    except Exception as e:
        print(f"mDNS registration failed (non-fatal): {e}")
    print("=" * 50 + "\n")

    yield  # Server đang chạy

    # Shutdown
    try:
        await loop.run_in_executor(None, zeroconf.unregister_service, mdns_info)
    except Exception:
        pass
    await loop.run_in_executor(None, zeroconf.close)


app = FastAPI(title="Smart Car Access Server", lifespan=lifespan)


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
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS friend_keys (
            friend_id   TEXT PRIMARY KEY,
            vehicle_id  TEXT NOT NULL,
            friend_key  TEXT NOT NULL,
            friend_name TEXT,
            expires_at  TEXT NOT NULL,
            owner_sig   TEXT NOT NULL,
            is_revoked  INTEGER DEFAULT 0,
            created_at  TEXT NOT NULL,
            claim_token TEXT UNIQUE NOT NULL
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


# --- Friend key DB helpers ---

def db_create_friend_key(vehicle_id: str, friend_name: str, ttl_hours: int) -> dict:
    friend_id = secrets.token_hex(8)
    friend_key = os.urandom(16)
    friend_key_hex = friend_key.hex()
    expires_at = (datetime.utcnow() + timedelta(hours=ttl_hours)).isoformat()
    created_at = datetime.utcnow().isoformat()
    claim_token = secrets.token_urlsafe(24)

    sig_bytes = sign_friend_bundle(friend_id, vehicle_id, friend_key_hex, expires_at)
    owner_sig = base64.b64encode(sig_bytes).decode()

    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO friend_keys
            (friend_id, vehicle_id, friend_key, friend_name, expires_at, owner_sig, is_revoked, created_at, claim_token)
        VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)
    ''', (friend_id, vehicle_id, base64.b64encode(friend_key).decode(),
          friend_name, expires_at, owner_sig, created_at, claim_token))
    conn.commit()
    conn.close()

    return {"friend_id": friend_id, "claim_token": claim_token, "expires_at": expires_at}


def db_get_friend_key_by_token(claim_token: str) -> Optional[dict]:
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()
    cursor.execute('''
        SELECT friend_id, vehicle_id, friend_key, friend_name, expires_at, owner_sig, is_revoked, created_at
        FROM friend_keys WHERE claim_token = ?
    ''', (claim_token,))
    row = cursor.fetchone()
    conn.close()
    if row:
        return {
            "friend_id": row[0], "vehicle_id": row[1],
            "friend_key": base64.b64decode(row[2]), "friend_name": row[3],
            "expires_at": row[4], "owner_sig": row[5],
            "is_revoked": row[6], "created_at": row[7]
        }
    return None


def db_get_friend_key_by_id(friend_id: str) -> Optional[dict]:
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()
    cursor.execute('''
        SELECT friend_id, vehicle_id, friend_key, friend_name, expires_at, owner_sig, is_revoked, created_at
        FROM friend_keys WHERE friend_id = ?
    ''', (friend_id,))
    row = cursor.fetchone()
    conn.close()
    if row:
        return {
            "friend_id": row[0], "vehicle_id": row[1],
            "friend_key": base64.b64decode(row[2]), "friend_name": row[3],
            "expires_at": row[4], "owner_sig": row[5],
            "is_revoked": row[6], "created_at": row[7]
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


class SecureCheckRequest(BaseModel):
    vehicle_id: str
    client_public_key_b64: str


class SecureCheckResponse(BaseModel):
    server_public_key_b64: str
    encrypted_data_b64: str
    nonce_b64: str


@app.post("/secure-check-pairing", response_model=SecureCheckResponse)
def secure_check_pairing(req: SecureCheckRequest):
    """Securely check if a vehicle is paired using ECDH + AES-GCM encryption"""

    # Load client public key
    try:
        client_pub_bytes = base64.b64decode(req.client_public_key_b64)
        client_public_key = serialization.load_der_public_key(client_pub_bytes)
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid client public key")

    # Generate server ephemeral EC key
    server_private_key = ec.generate_private_key(ec.SECP256R1())
    server_public_key = server_private_key.public_key()

    # Perform ECDH
    shared_secret = server_private_key.exchange(ec.ECDH(), client_public_key)

    # Derive KEK
    kek = HKDF(
        algorithm=hashes.SHA256(),
        length=16,
        salt=None,
        info=b"secure-check-kek",
    ).derive(shared_secret)

    # Get pairing status
    vehicle_data = get_vehicle_pairing(req.vehicle_id)

    if vehicle_data:
        response_data = {
            "paired": True,
            "vehicle_id": req.vehicle_id,
            "pairing_id": vehicle_data["pairing_id"],
            "paired_at": vehicle_data["created_at"],
            "pairing_key": vehicle_data["pairing_key"].hex()  # ← THÊM DÒNG NÀY!
        }
    else:
        response_data = {
            "paired": False,
            "vehicle_id": req.vehicle_id,
            "message": "Vehicle not paired"
        }

    # Convert to JSON string
    import json
    response_json = json.dumps(response_data)

    # Encrypt response
    aesgcm = AESGCM(kek)
    nonce = os.urandom(12)
    encrypted_data = aesgcm.encrypt(nonce, response_json.encode(), None)

    # Serialize server public key
    server_pub_bytes = server_public_key.public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )

    print(f"✓ Secure check for vehicle {req.vehicle_id}: {'PAIRED' if vehicle_data else 'NOT PAIRED'}")

    return SecureCheckResponse(
        server_public_key_b64=base64.b64encode(server_pub_bytes).decode(),
        encrypted_data_b64=base64.b64encode(encrypted_data).decode(),
        nonce_b64=base64.b64encode(nonce).decode()
    )


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


# ============================================================
#  Friend Sharing
# ============================================================

class FriendShareCreateRequest(BaseModel):
    vehicle_id: str
    friend_name: str = "Friend"
    ttl_hours: int = 24  # 1 – 720 hours


class FriendShareCreateResponse(BaseModel):
    friend_id: str
    claim_token: str
    claim_url: str
    expires_at: str


class FriendKeyBundle(BaseModel):
    friend_id: str
    vehicle_id: str
    friend_key_hex: str
    friend_name: str
    expires_at: str
    owner_sig_b64: str


class ValidateFriendKeyRequest(BaseModel):
    vehicle_id: str
    friend_id: str
    owner_sig_b64: str


class ValidateFriendKeyResponse(BaseModel):
    valid: bool
    friend_key_hex: str
    expires_at: str


@app.post("/friend-sharing/create", response_model=FriendShareCreateResponse)
def create_friend_share(req: FriendShareCreateRequest):
    """Owner creates a friend sharing link. Returns a claim token/URL the owner sends to the friend."""
    if not get_vehicle_pairing(req.vehicle_id):
        raise HTTPException(status_code=404, detail="Vehicle not found")
    if not (1 <= req.ttl_hours <= 720):
        raise HTTPException(status_code=400, detail="ttl_hours must be between 1 and 720")

    result = db_create_friend_key(req.vehicle_id, req.friend_name, req.ttl_hours)
    print(f"✓ Friend share created: {result['friend_id']} for vehicle {req.vehicle_id}, expires {result['expires_at']}")

    local_ip = get_local_ip()
    return FriendShareCreateResponse(
        friend_id=result["friend_id"],
        claim_token=result["claim_token"],
        claim_url=f"http://{local_ip}:8000/friend-sharing/claim/{result['claim_token']}",
        expires_at=result["expires_at"]
    )


@app.get("/friend-sharing/claim/{claim_token}", response_model=FriendKeyBundle)
def claim_friend_key(claim_token: str):
    """Friend retrieves key bundle using the claim token from the share link."""
    data = db_get_friend_key_by_token(claim_token)
    if not data:
        raise HTTPException(status_code=404, detail="Invalid claim token")
    if data["is_revoked"]:
        raise HTTPException(status_code=410, detail="This share has been revoked")
    if datetime.utcnow() > datetime.fromisoformat(data["expires_at"]):
        raise HTTPException(status_code=410, detail="This share has expired")

    print(f"✓ Friend key claimed: {data['friend_id']} for vehicle {data['vehicle_id']}")
    return FriendKeyBundle(
        friend_id=data["friend_id"],
        vehicle_id=data["vehicle_id"],
        friend_key_hex=data["friend_key"].hex(),
        friend_name=data["friend_name"] or "Friend",
        expires_at=data["expires_at"],
        owner_sig_b64=data["owner_sig"]
    )


@app.post("/validate-friend-key", response_model=ValidateFriendKeyResponse)
def validate_friend_key(req: ValidateFriendKeyRequest):
    """Anchor calls this to validate a friend key (cache miss). Returns friend_key + TTL on success, 401 if revoked/expired."""
    data = db_get_friend_key_by_id(req.friend_id)
    if not data:
        raise HTTPException(status_code=404, detail="Friend key not found")
    if data["vehicle_id"] != req.vehicle_id:
        raise HTTPException(status_code=403, detail="Friend key does not belong to this vehicle")
    if data["is_revoked"]:
        raise HTTPException(status_code=401, detail="key revoked")

    expires_at = datetime.fromisoformat(data["expires_at"])
    if datetime.utcnow() > expires_at:
        raise HTTPException(status_code=401, detail="key expired")

    try:
        sig_bytes = base64.b64decode(req.owner_sig_b64)
        sig_ok = verify_friend_bundle_sig(
            data["friend_id"], data["vehicle_id"],
            data["friend_key"].hex(), data["expires_at"],
            sig_bytes
        )
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid signature format")

    if not sig_ok:
        raise HTTPException(status_code=401, detail="Invalid owner signature")

    print(f"✓ Friend key validated: {req.friend_id} for vehicle {req.vehicle_id}")

    return ValidateFriendKeyResponse(
        valid=True,
        friend_key_hex=data["friend_key"].hex(),
        expires_at=data["expires_at"]
    )


@app.delete("/friend-sharing/{friend_id}")
def revoke_friend_key(friend_id: str):
    """Owner revokes a friend sharing key immediately."""
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()
    cursor.execute('UPDATE friend_keys SET is_revoked = 1 WHERE friend_id = ?', (friend_id,))
    updated = cursor.rowcount
    conn.commit()
    conn.close()

    if updated == 0:
        raise HTTPException(status_code=404, detail="Friend key not found")

    print(f"✓ Friend key revoked: {friend_id}")
    return {"message": "Friend key revoked", "friend_id": friend_id}


@app.get("/friend-sharing/list/{vehicle_id}")
def list_friend_keys(vehicle_id: str):
    """List all friend sharing keys for a vehicle (active, expired, revoked)."""
    conn = sqlite3.connect('car_access.db')
    cursor = conn.cursor()
    cursor.execute('''
        SELECT friend_id, friend_name, expires_at, is_revoked, created_at
        FROM friend_keys WHERE vehicle_id = ?
        ORDER BY created_at DESC
    ''', (vehicle_id,))
    rows = cursor.fetchall()
    conn.close()

    now = datetime.utcnow()

    def status(is_revoked, expires_at_str):
        if is_revoked:
            return "revoked"
        if now > datetime.fromisoformat(expires_at_str):
            return "expired"
        return "active"

    return {
        "vehicle_id": vehicle_id,
        "total": len(rows),
        "friend_keys": [
            {
                "friend_id": r[0],
                "friend_name": r[1],
                "expires_at": r[2],
                "is_revoked": bool(r[3]),
                "created_at": r[4],
                "status": status(r[3], r[2])
            }
            for r in rows
        ]
    }


@app.get("/server-public-key")
def get_server_public_key():
    """Return server signing public key (DER, base64). Anchor can fetch this once and cache for offline bundle verification."""
    pub = get_server_signing_key().public_key()
    pub_bytes = pub.public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )
    return {"server_public_key_b64": base64.b64encode(pub_bytes).decode()}


@app.get("/")
def root():
    return {
        "message": "Smart Car Access Server",
        "database": "SQLite (car_access.db)",
        "endpoints": {
            "POST /secure-check-pairing": "Securely check if vehicle is paired (encrypted)",
            "GET /check-pairing/{vehicle_id}": "Check if vehicle is paired (unencrypted)",
            "POST /owner-pairing": "Pair vehicle and store key in database",
            "GET /vehicle/{vehicle_id}": "Get vehicle info",
            "GET /vehicles": "List all vehicles",
            "DELETE /vehicle/{vehicle_id}": "Delete vehicle",
            "POST /friend-sharing/create": "Owner creates a friend share link",
            "GET /friend-sharing/claim/{claim_token}": "Friend claims key bundle from share link",
            "POST /validate-friend-key": "Anchor validates a friend key (online check)",
            "DELETE /friend-sharing/{friend_id}": "Owner revokes a friend key",
            "GET /friend-sharing/list/{vehicle_id}": "List all friend keys for a vehicle",
            "GET /server-public-key": "Get server signing public key"
        }
    }


