package com.example.uwb.repository

import com.example.uwb.model.PairingRequest
import com.example.uwb.crypto.HKDF_SHA256
import com.example.uwb.crypto.AesGcmUtil
import com.example.uwb.crypto.GeneratePrivateKeyEcc
import com.example.uwb.network.ApiClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.security.KeyFactory
import java.security.spec.X509EncodedKeySpec
import java.util.Base64
import javax.crypto.KeyAgreement

class PairingRepository {

    suspend fun pairVehicle(vin: String): ByteArray = withContext(Dispatchers.IO) {

        // 1️⃣ Tạo EC key pair (RAM)
        val pubBytes = GeneratePrivateKeyEcc.getOrCreateKeyPair()
        val pubB64 = Base64.getEncoder().encodeToString(pubBytes)

        // 2️⃣ Gửi public key lên server
        val resp = ApiClient.api.ownerPairing(
            PairingRequest(vin, pubB64)
        )

        // 3️⃣ Decode public key server
        val serverPub = KeyFactory.getInstance("EC")
            .generatePublic(
                X509EncodedKeySpec(
                    Base64.getDecoder().decode(resp.server_public_key_b64)
                )
            )

        // 4️⃣ ECDH (KHÔNG Android Keystore)
        val keyAgreement = KeyAgreement.getInstance("ECDH")
        keyAgreement.init(GeneratePrivateKeyEcc.getPrivateKey())
        keyAgreement.doPhase(serverPub, true)
        val sharedSecret = keyAgreement.generateSecret()

        // 5️⃣ HKDF → KEK
        val kek = HKDF_SHA256.hkdf(sharedSecret)

        // 6️⃣ Giải mã pairing key
        AesGcmUtil.decrypt(
            kek,
            Base64.getDecoder().decode(resp.nonce_b64),
            Base64.getDecoder().decode(resp.encrypted_pairing_key_b64)
        )
    }
}
