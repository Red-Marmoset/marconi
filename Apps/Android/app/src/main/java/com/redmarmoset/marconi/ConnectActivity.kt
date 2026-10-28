// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The first screen: an address, an optional user name and password, and Connect.

package com.redmarmoset.marconi

import android.content.Intent
import android.os.Bundle
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class ConnectActivity : AppCompatActivity() {
    private lateinit var address: EditText
    private lateinit var username: EditText
    private lateinit var password: EditText
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)
        address = findViewById(R.id.address)
        username = findViewById(R.id.username)
        password = findViewById(R.id.password)
        status = findViewById(R.id.status)

        val preferences = getSharedPreferences("marconi", MODE_PRIVATE)
        address.setText(intent?.data?.toString() ?: preferences.getString("address", ""))
        username.setText(preferences.getString("username", ""))

        findViewById<Button>(R.id.connect).setOnClickListener { connect() }
        password.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_GO) {
                connect()
                true
            } else false
        }
        status.text = intent?.getStringExtra(ViewerActivity.EXTRA_STATUS) ?: ""
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        status.text = intent.getStringExtra(ViewerActivity.EXTRA_STATUS) ?: ""
        intent.data?.let { address.setText(it.toString()) }
    }

    private fun connect() {
        val text = address.text.toString().trim()
        if (text.isEmpty()) {
            address.error = getString(R.string.address_hint)
            return
        }
        getSharedPreferences("marconi", MODE_PRIVATE).edit()
            .putString("address", text)
            .putString("username", username.text.toString())
            .apply()
        status.text = ""
        startActivity(Intent(this, ViewerActivity::class.java)
            .putExtra(ViewerActivity.EXTRA_ADDRESS, text)
            .putExtra(ViewerActivity.EXTRA_USERNAME, username.text.toString())
            .putExtra(ViewerActivity.EXTRA_PASSWORD, password.text.toString()))
    }
}
