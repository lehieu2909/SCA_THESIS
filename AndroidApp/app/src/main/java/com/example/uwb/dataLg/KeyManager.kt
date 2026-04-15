package com.example.uwb.dataLg

import android.util.Log
import java.util.*

/**
 * KeyManager: Singleton lưu trữ pairing key từ server
 * Key này được server generate, encrypt, app decrypt, và lưu để gửi cho tag
 */
object KeyManager {
    private var pairingKey: ByteArray? = null
    private var vehicleId: String? = null
    private var pairingId: String? = null
    private var timestamp: Long = 0

    fun savePairingKey(vId: String, pId: String, key: ByteArray) {
        vehicleId = vId
        pairingId = pId
        pairingKey = key
        timestamp = System.currentTimeMillis()
        Log.d("KeyManager", "✓ Pairing key saved for vehicle: $vId (${key.size} bytes)")
    }

    fun getPairingKey(): ByteArray? = pairingKey

    fun getVehicleId(): String? = vehicleId

    fun getPairingId(): String? = pairingId

    fun getTimestamp(): Long = timestamp

    fun getPairingKeyHex(): String {
        return pairingKey?.let {
            it.joinToString("") { b -> "%02x".format(b) }
        } ?: "NOT_SET"
    }

    fun clearKey() {
        pairingKey = null
        vehicleId = null
        pairingId = null
        timestamp = 0
        Log.d("KeyManager", "✓ Pairing key cleared")
    }

    fun isKeySet(): Boolean = pairingKey != null && vehicleId != null
}
