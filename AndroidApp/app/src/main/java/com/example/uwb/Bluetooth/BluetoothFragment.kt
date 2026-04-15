package com.example.uwb.Bluetooth

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.*
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.*
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.annotation.RequiresApi
import androidx.core.app.ActivityCompat
import androidx.fragment.app.Fragment
import com.example.uwb.databinding.FragmentBluetoothBinding
import com.example.uwb.transport.UsbTransport
import com.example.uwb.transport.TransportHolder
import com.example.uwb.UI.PairingLoadingFragment
import com.example.uwb.dataLg.KeyManager

class BluetoothFragment : Fragment() {

    private var _binding: FragmentBluetoothBinding? = null
    private val binding get() = _binding!!

    private val bluetoothAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private val deviceList = mutableListOf<BluetoothDevice>()
    private val deviceNameList = mutableListOf<String>()
    private lateinit var listAdapter: ArrayAdapter<String>

    private lateinit var usbTransport: UsbTransport

    private val ESP32_VENDOR_ID = 0x303A
    private val SCAN_PERIOD_MS = 10000L

    // BLE scan callback — chỉ nhận BLE advertisements (không phải Classic Bluetooth)
    private val bleScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            if (ActivityCompat.checkSelfPermission(
                    requireContext(), Manifest.permission.BLUETOOTH_CONNECT
                ) != PackageManager.PERMISSION_GRANTED
            ) return
            // Bỏ qua nếu đã có trong danh sách
            if (deviceList.none { it.address == device.address }) {
                deviceList.add(device)
                // Xử lý device name an toàn - tránh ký tự bậy
                val name = try {
                    device.name?.takeIf { it.isNotEmpty() } ?: "Unknown"
                } catch (e: Exception) {
                    "Unknown"
                }
                deviceNameList.add("$name\n${device.address}")
                requireActivity().runOnUiThread { listAdapter.notifyDataSetChanged() }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            requireActivity().runOnUiThread {
                Toast.makeText(requireContext(), "BLE scan lỗi: $errorCode", Toast.LENGTH_SHORT).show()
            }
        }
    }

    // Receiver: bắt ESP32-S3 cắm vào khi app đang chạy
    private val usbAttachReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
                val device: UsbDevice? = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                device?.let { connectUsbDevice(it) }
            }
        }
    }

    @RequiresApi(Build.VERSION_CODES.S)
    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentBluetoothBinding.inflate(inflater, container, false)

        listAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_list_item_1, deviceNameList)
        binding.rvBluetooth.adapter = listAdapter

        usbTransport = UsbTransport(requireContext())

        requireContext().registerReceiver(
            usbAttachReceiver,
            IntentFilter(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        )

        // Kết nối ESP32-S3 qua USB nếu đã cắm sẵn
        findAndConnectEsp32()

        // Nhấn vào thiết bị BLE → chuyển sang màn hình pairing loading
        binding.rvBluetooth.setOnItemClickListener { _, _, position, _ ->
            val device = deviceList[position]
            val mac = device.address.lowercase()
            val name = if (ActivityCompat.checkSelfPermission(
                    requireContext(), Manifest.permission.BLUETOOTH_CONNECT
                ) == PackageManager.PERMISSION_GRANTED
            ) device.name ?: "Unknown" else "Unknown"

            val pairingFragment = PairingLoadingFragment().apply {
                arguments = Bundle().also {
                    it.putString("DEVICE_MAC", mac)
                    it.putString("DEVICE_NAME", name)
                }
            }

            requireActivity().supportFragmentManager.beginTransaction()
                .replace(com.example.uwb.R.id.fragment_container, pairingFragment)
                .addToBackStack(null)
                .commit()
        }

        requestBluetoothPermissions()
        binding.btnRefresh.setOnClickListener { scanBle() }

        return binding.root
    }

    private fun findAndConnectEsp32() {
        val usbManager = requireContext().getSystemService(Context.USB_SERVICE) as UsbManager
        val esp32 = usbManager.deviceList.values.find { it.vendorId == ESP32_VENDOR_ID }
        if (esp32 != null) {
            connectUsbDevice(esp32)
        } else {
            Toast.makeText(requireContext(), "Chưa thấy ESP32-S3 — hãy cắm cáp USB", Toast.LENGTH_LONG).show()
        }
    }

    private fun connectUsbDevice(device: UsbDevice) {
        usbTransport.openDevice(device) {
            // Lưu transport vào holder để UwbFragment dùng lại
            TransportHolder.transport = usbTransport

            // Nhận phản hồi từ S3 (FOUND, CONNECTED, CONNECT_FAILED, BLE_DISCONNECTED, ...)
            usbTransport.receive { data ->
                val msg = String(data).trim()
                android.util.Log.d("S3_MSG", msg)   // xem trong Logcat
                requireActivity().runOnUiThread {
                    Toast.makeText(requireContext(), "S3: $msg", Toast.LENGTH_SHORT).show()
                }
            }
            requireActivity().runOnUiThread {
                Toast.makeText(requireContext(), "Đã kết nối ESP32-S3 qua USB", Toast.LENGTH_SHORT).show()
            }
        }
    }

    /**
     * Quét BLE advertisements trong 10 giây.
     * DevKit V1 sẽ xuất hiện trong danh sách với tên "ESP32-DevKit-BLE".
     */
    private fun scanBle() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
            startActivity(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            return
        }
        if (ActivityCompat.checkSelfPermission(
                requireContext(), Manifest.permission.BLUETOOTH_SCAN
            ) != PackageManager.PERMISSION_GRANTED
        ) return

        deviceList.clear()
        deviceNameList.clear()
        listAdapter.notifyDataSetChanged()

        bluetoothAdapter.bluetoothLeScanner?.startScan(bleScanCallback)
        Toast.makeText(requireContext(), "Đang quét BLE (10s)...", Toast.LENGTH_SHORT).show()

        // Dừng scan sau 10 giây để tiết kiệm pin
        Handler(Looper.getMainLooper()).postDelayed({
            if (ActivityCompat.checkSelfPermission(
                    requireContext(), Manifest.permission.BLUETOOTH_SCAN
                ) == PackageManager.PERMISSION_GRANTED
            ) {
                bluetoothAdapter.bluetoothLeScanner?.stopScan(bleScanCallback)
            }
        }, SCAN_PERIOD_MS)
    }

    @RequiresApi(Build.VERSION_CODES.S)
    private fun requestBluetoothPermissions() {
        ActivityCompat.requestPermissions(
            requireActivity(),
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            ),
            1
        )
    }

    override fun onDestroyView() {
        super.onDestroyView()
        if (ActivityCompat.checkSelfPermission(
                requireContext(), Manifest.permission.BLUETOOTH_SCAN
            ) == PackageManager.PERMISSION_GRANTED
        ) {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(bleScanCallback)
        }
        requireContext().unregisterReceiver(usbAttachReceiver)
        // Không disconnect USB ở đây — UwbFragment sẽ dùng lại transport này
        _binding = null
    }
}
