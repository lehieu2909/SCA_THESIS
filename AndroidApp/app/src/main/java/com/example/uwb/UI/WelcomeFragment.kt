package com.example.uwb.UI

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import com.example.uwb.databinding.FragmentWelcomeBinding

class WelcomeFragment : Fragment() {

    private var _binding: FragmentWelcomeBinding? = null
    private val binding get() = _binding!!

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentWelcomeBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        // Ánh xạ & xử lý button OWPA
        binding.btnOwpa.setOnClickListener {
            onOwpaClicked()
        }
    }

    private fun onOwpaClicked() {
        // TODO: mở dialog đăng nhập OWPA
        // hoặc navigate sang Fragment khác
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null // tránh memory leak
    }
}
