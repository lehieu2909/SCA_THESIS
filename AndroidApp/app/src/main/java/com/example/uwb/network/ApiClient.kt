package com.example.uwb.network
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
object ApiClient {
    val api: ApiService by lazy {
        Retrofit.Builder()
            .baseUrl("http://192.168.1.151:8000") // IP máy chạy FastAPI
            .addConverterFactory(GsonConverterFactory.create())
            .build()
            .create(ApiService::class.java)
    }
}