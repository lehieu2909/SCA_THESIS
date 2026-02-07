package com.example.uwb.network
import retrofit2.http.Body
import retrofit2.http.POST
import com.example.uwb.model.PairingRequest
import com.example.uwb.model.PairingResponse

interface ApiService {
    @POST("/owner-pairing")
    suspend fun ownerPairing(
        @Body req: PairingRequest
    ): PairingResponse
}