package com.example.uwb.crypto

import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.PrivateKey

object GeneratePrivateKeyEcc {

    private var keyPair: KeyPair? = null

    fun getOrCreateKeyPair(): ByteArray {
        if (keyPair == null) {
            val kpg = KeyPairGenerator.getInstance("EC")
            kpg.initialize(256) // secp256r1
            keyPair = kpg.generateKeyPair()
        }
        return keyPair!!.public.encoded
    }

    fun getPrivateKey(): PrivateKey {
        return keyPair!!.private
    }
}
