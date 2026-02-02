package com.example.uwb.UI

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import com.example.uwb.databinding.FragmentEnterVinBinding

class EnterVinFragment : Fragment() {

    private var _binding: FragmentEnterVinBinding? = null
    private val binding get() = _binding!!

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

        // Ví dụ thao tác với view
        // binding.btnSubmitVin.setOnClickListener { ... }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
