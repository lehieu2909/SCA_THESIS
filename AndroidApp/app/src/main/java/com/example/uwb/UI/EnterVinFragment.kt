package com.example.uwb.UI

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.example.uwb.Bluetooth.BluetoothFragment
import com.example.uwb.VinValid.VinValidator
import com.example.uwb.databinding.FragmentEnterVinBinding
import com.example.uwb.repository.PairingRepository
import com.example.uwb.dataLg.KeyManager
import kotlinx.coroutines.launch

class EnterVinFragment : Fragment() {

    private var _binding: FragmentEnterVinBinding? = null
    private val binding get() = _binding!!

    private val repo = PairingRepository()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {

        _binding = FragmentEnterVinBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        binding.btnConfirm.setOnClickListener {

            val vin = binding.edtVin.text.toString().trim()

            if (!VinValidator.isValid(vin)) {
                binding.edtVin.error = "VIN không hợp lệ"
                return@setOnClickListener
            }

            sendVinToServer(vin)
        }
    }

    private fun sendVinToServer(vin: String) {

        lifecycleScope.launch {

            try {

                val result = repo.pairVehicle(vin)

                KeyManager.savePairingKey(
                    vId = vin,
                    pId = result.pairingId,
                    key = result.pairingKey
                )

                Toast.makeText(
                    requireContext(),
                    "✓ Key nhận thành công (${result.pairingKey.size} bytes)",
                    Toast.LENGTH_SHORT
                ).show()

                // Chuyển sang Bluetooth fragment
                requireActivity().supportFragmentManager.beginTransaction()
                    .replace(
                        com.example.uwb.R.id.fragment_container,
                        BluetoothFragment()
                    )
                    .commit()

            } catch (e: Exception) {
                binding.edtVin.error = "Không thể kết nối server"
                e.printStackTrace()
                binding.edtVin.error = e.message
            }
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}