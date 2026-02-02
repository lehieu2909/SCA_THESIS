package com.example.uwb.model

data class PairingResponse(
    val pairing_id: String,
    val server_public_key_b64: String,
    val encrypted_pairing_key_b64: String,
    val nonce_b64: String
)
