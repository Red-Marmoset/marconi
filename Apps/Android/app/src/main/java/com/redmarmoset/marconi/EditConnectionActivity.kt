// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Adding or changing a saved computer: a name, its address and an optional user name. Save keeps it for the list;
// Connect saves it and connects straight away.

package com.redmarmoset.marconi

import android.content.Intent
import android.os.Bundle
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity

class EditConnectionActivity : AppCompatActivity() {
    private lateinit var name: EditText
    private lateinit var address: EditText
    private lateinit var username: EditText
    private var previousName: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_edit_connection)
        name = findViewById(R.id.name)
        address = findViewById(R.id.address)
        username = findViewById(R.id.username)

        previousName = intent.getStringExtra(EXTRA_NAME)
        title = getString(if (previousName == null) R.string.add_computer else R.string.edit_computer)
        name.setText(previousName ?: "")
        address.setText(intent.getStringExtra(EXTRA_ADDRESS) ?: "")
        username.setText(intent.getStringExtra(EXTRA_USERNAME) ?: "")

        findViewById<Button>(R.id.save).setOnClickListener { if (save() != null) finish() }
        findViewById<Button>(R.id.connect).setOnClickListener { save()?.let { connect(it) } }
        username.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_GO) { save()?.let { connect(it) }; true } else false
        }
    }

    /** Checks the fields and saves the computer; null when something is missing. */
    private fun save(): SavedConnection? {
        val addressText = address.text.toString().trim()
        if (addressText.isEmpty()) {
            address.error = getString(R.string.address_needed)
            return null
        }
        val nameText = name.text.toString().trim().ifEmpty { addressText }
        val connection = SavedConnection(nameText, addressText, username.text.toString().trim())
        SavedConnections(this).save(connection, previousName)
        return connection
    }

    private fun connect(connection: SavedConnection) {
        startActivity(Intent(this, ViewerActivity::class.java)
            .putExtra(ViewerActivity.EXTRA_ADDRESS, connection.address)
            .putExtra(ViewerActivity.EXTRA_USERNAME, connection.username)
            .putExtra(ViewerActivity.EXTRA_PASSWORD, ""))
        finish()
    }

    companion object {
        const val EXTRA_NAME = "name"
        const val EXTRA_ADDRESS = "address"
        const val EXTRA_USERNAME = "username"
    }
}
