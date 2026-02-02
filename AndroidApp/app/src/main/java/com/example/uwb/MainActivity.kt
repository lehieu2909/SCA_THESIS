package com.example.uwb
import com.example.uwb.databinding.ActivityMainBinding

import android.os.Bundle
import android.util.Log
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.example.uwb.UI.WelcomeFragment
import com.example.uwb.VinValid.VinValidator
import kotlinx.coroutines.launch
import com.example.uwb.repository.PairingRepository


class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Mở WelcomeFragment ngay khi chạy app
        if (savedInstanceState == null) {
            supportFragmentManager.beginTransaction()
                .replace(R.id.fragmentContainer, WelcomeFragment())
                .commit()
        }
    }
}
