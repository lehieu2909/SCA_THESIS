package com.example.uwb.UI

import android.animation.ObjectAnimator
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.LinearInterpolator
import android.widget.Toast
import androidx.fragment.app.Fragment
import com.example.uwb.R
import com.example.uwb.databinding.FragmentPairingLoadingBinding
import com.example.uwb.transport.TransportHolder
import com.example.uwb.transport.UsbTransport

class PairingLoadingFragment : Fragment() {

    private var _binding: FragmentPairingLoadingBinding? = null
    private val binding get() = _binding!!

    private lateinit var usbTransport: UsbTransport
    private var carAnimator: ObjectAnimator? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    private var deviceMac: String? = null
    private var deviceName: String? = null

    // Tránh xử lý kết quả nhiều lần (CONNECTED + NOT_FOUND có thể đến liên tiếp)
    private var resultHandled = false

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentPairingLoadingBinding.inflate(inflater, container, false)

        deviceMac  = arguments?.getString("DEVICE_MAC")
        deviceName = arguments?.getString("DEVICE_NAME")

        val transport = TransportHolder.transport
        if (transport == null) {
            Toast.makeText(requireContext(), "Chưa kết nối USB với Tag", Toast.LENGTH_LONG).show()
            parentFragmentManager.popBackStack()
            return binding.root
        }
        usbTransport = transport

        binding.tvDeviceName.text = deviceName ?: ""
        binding.btnCancel.setOnClickListener { cancelAndGoBack() }

        startCarAnimation()
        sendConnectCommand()
        startTimeout()
        listenForResponse()

        return binding.root
    }

    // ── Gửi lệnh CONNECT xuống Tag ─────────────────────────────────────────
    private fun sendConnectCommand() {
        val mac = deviceMac ?: return
        usbTransport.send("CONNECT:$mac\n".toByteArray())
        updateStatus("Đang quét BLE...")
        Log.d("PairingLoading", "Sent CONNECT:$mac")
    }

    // ── Animation xe chạy ngang ────────────────────────────────────────────
    private fun startCarAnimation() {
        binding.roadTrack.post {
            val trackWidth = binding.roadTrack.width.toFloat()
            val carWidth   = binding.ivCar.width.toFloat()
            carAnimator = ObjectAnimator.ofFloat(
                binding.ivCar, "translationX", -carWidth, trackWidth
            ).apply {
                duration        = 1600
                repeatCount     = ObjectAnimator.INFINITE
                interpolator    = LinearInterpolator()
                start()
            }
        }
    }

    // ── Timeout 15 giây ────────────────────────────────────────────────────
    private fun startTimeout() {
        mainHandler.postDelayed({
            if (_binding != null && !resultHandled) {
                onPairingFailed("Hết thời gian chờ — thử lại")
            }
        }, 15_000L)
    }

    // ── Lắng nghe phản hồi từ Tag qua USB ─────────────────────────────────
    private fun listenForResponse() {
        usbTransport.receive { data ->
            val msg = String(data).trim()
            if (msg.isEmpty()) return@receive
            Log.d("PairingLoading", "S3: $msg")

            val act = activity ?: return@receive
            act.runOnUiThread {
                if (_binding == null || resultHandled) return@runOnUiThread
                when {
                    msg.startsWith("SCANNING")           -> updateStatus("Đang quét BLE...")
                    msg.startsWith("FOUND:")             -> updateStatus("Đã tìm thấy thiết bị")
                    msg.startsWith("BLE_CLIENT_CONNECTED") -> updateStatus("Đang xác thực kết nối...")
                    msg.startsWith("CONNECTED:")         -> onPairingSuccess()
                    msg.startsWith("NOT_FOUND:")         -> onPairingFailed("Không tìm thấy thiết bị")
                    msg == "CONNECT_FAILED"              -> onPairingFailed("Kết nối BLE thất bại")
                }
            }
        }
    }

    // ── Thành công ─────────────────────────────────────────────────────────
    private fun onPairingSuccess() {
        resultHandled = true
        mainHandler.removeCallbacksAndMessages(null)
        carAnimator?.cancel()

        updateStatus("Kết nối thành công!")

        // Hiển thị thành công 800ms rồi chuyển sang UWB
        mainHandler.postDelayed({
            if (_binding == null) return@postDelayed

            val uwbFragment = UwbFragment().apply {
                arguments = Bundle().also {
                    it.putString("DEVICE_MAC",  deviceMac)
                    it.putString("DEVICE_NAME", deviceName)
                }
            }

            // Pop màn loading ra khỏi back stack, thay bằng UWB
            parentFragmentManager.popBackStack()
            parentFragmentManager.beginTransaction()
                .replace(R.id.fragment_container, uwbFragment)
                .addToBackStack(null)
                .commit()
        }, 800L)
    }

    // ── Thất bại ───────────────────────────────────────────────────────────
    private fun onPairingFailed(reason: String) {
        resultHandled = true
        mainHandler.removeCallbacksAndMessages(null)
        carAnimator?.cancel()

        updateStatus("Kết nối thất bại")
        Toast.makeText(requireContext(), reason, Toast.LENGTH_LONG).show()

        // Quay về Bluetooth sau 1.2s
        mainHandler.postDelayed({
            if (_binding != null) parentFragmentManager.popBackStack()
        }, 1200L)
    }

    // ── Hủy thủ công ───────────────────────────────────────────────────────
    private fun cancelAndGoBack() {
        resultHandled = true
        mainHandler.removeCallbacksAndMessages(null)
        carAnimator?.cancel()
        parentFragmentManager.popBackStack()
    }

    private fun updateStatus(status: String) {
        _binding?.tvStatus?.text = status
    }

    override fun onDestroyView() {
        super.onDestroyView()
        carAnimator?.cancel()
        mainHandler.removeCallbacksAndMessages(null)
        _binding = null
    }
}
