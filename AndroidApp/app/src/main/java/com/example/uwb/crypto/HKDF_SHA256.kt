package com.example.uwb.crypto

import org.bouncycastle.crypto.digests.SHA256Digest
import org.bouncycastle.crypto.generators.HKDFBytesGenerator
import org.bouncycastle.crypto.params.HKDFParameters

object HKDF_SHA256 {

    fun hkdf(sharedSecret: ByteArray): ByteArray {

        val salt = "smart-car-access-salt".toByteArray(Charsets.UTF_8)
        val info = byteArrayOf(0x20) // ASCII space ' '

        val hkdf = HKDFBytesGenerator(SHA256Digest())
        hkdf.init(
            HKDFParameters(
                sharedSecret,
                salt,
                info
            )
        )

        val kek = ByteArray(16)
        hkdf.generateBytes(kek, 0, kek.size)

        return kek
    }
}
