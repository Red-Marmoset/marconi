// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Adding or changing a saved connection, with the same choices as the desktop's connection dialog.

package com.redmarmosetstudios.marconi

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.RadioGroup
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.textfield.MaterialAutoCompleteTextView

class EditConnectionActivity : AppCompatActivity() {
    private lateinit var store: SavedConnections
    private lateinit var connection: SavedConnection
    private var isNew = true

    private lateinit var name: EditText
    private lateinit var group: MaterialAutoCompleteTextView
    private lateinit var address: EditText
    private lateinit var username: EditText
    private lateinit var password: EditText
    private lateinit var rememberPassword: MaterialSwitch
    private lateinit var mac: EditText
    private lateinit var notes: EditText
    private lateinit var favourite: MaterialSwitch
    private lateinit var scale: RadioGroup
    private lateinit var quality: RadioGroup
    private lateinit var viewOnly: MaterialSwitch
    private lateinit var clipboard: MaterialSwitch
    private lateinit var lock: MaterialSwitch
    private lateinit var sshTunnel: MaterialSwitch

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_edit_connection)
        store = SavedConnections(this)
        val existing = intent.getStringExtra(EXTRA_ID)?.let { store.load(it) }
        isNew = existing == null
        connection = existing ?: SavedConnection()

        val toolbar = findViewById<MaterialToolbar>(R.id.toolbar)
        toolbar.title = getString(if (isNew) R.string.new_connection else R.string.edit_connection)
        toolbar.setNavigationOnClickListener { finish() }

        name = findViewById(R.id.name)
        group = findViewById(R.id.group)
        address = findViewById(R.id.address)
        username = findViewById(R.id.username)
        password = findViewById(R.id.password)
        rememberPassword = findViewById(R.id.rememberPassword)
        mac = findViewById(R.id.mac)
        notes = findViewById(R.id.notes)
        favourite = findViewById(R.id.favourite)
        scale = findViewById(R.id.scale)
        quality = findViewById(R.id.quality)
        viewOnly = findViewById(R.id.viewOnly)
        clipboard = findViewById(R.id.clipboard)
        lock = findViewById(R.id.lock)
        sshTunnel = findViewById(R.id.sshTunnel)

        group.setAdapter(ArrayAdapter(this, android.R.layout.simple_list_item_1, store.groups()))
        name.setText(connection.name)
        group.setText(connection.group, false)
        address.setText(connection.address)
        username.setText(connection.username)
        mac.setText(connection.macAddress)
        notes.setText(connection.notes)
        favourite.isChecked = connection.favourite
        scale.check(if (connection.scaleFit) R.id.scaleFit else R.id.scaleActual)
        quality.check(when (connection.quality) {
            SavedConnection.QUALITY_BEST -> R.id.qualityBest
            SavedConnection.QUALITY_LOW -> R.id.qualityLow
            else -> R.id.qualityBalanced
        })
        viewOnly.isChecked = connection.viewOnly
        clipboard.isChecked = connection.clipboard
        lock.isChecked = connection.lockOnDisconnect
        sshTunnel.isChecked = connection.sshTunnel
        if (store.isPasswordStoreAvailable()) {
            rememberPassword.isChecked = connection.rememberPassword
            if (connection.rememberPassword) password.setText(store.loadPassword(connection.id) ?: "")
        } else {
            rememberPassword.isChecked = false
            rememberPassword.isEnabled = false
            findViewById<View>(R.id.noPasswordStore).visibility = View.VISIBLE
        }

        findViewById<Button>(R.id.save).setOnClickListener { if (save()) finish() }
        findViewById<Button>(R.id.connect).setOnClickListener { if (save()) connect() }
        findViewById<Button>(R.id.delete).apply {
            visibility = if (isNew) View.GONE else View.VISIBLE
            setOnClickListener { confirmDelete() }
        }
    }

    /** Checks the fields and saves; false when something is missing. */
    private fun save(): Boolean {
        val addressText = address.text.toString().trim()
        if (addressText.isEmpty()) {
            address.error = getString(R.string.address_needed)
            address.requestFocus()
            return false
        }
        connection.name = name.text.toString().trim()
        connection.group = group.text.toString().trim()
        connection.address = addressText
        connection.username = username.text.toString().trim()
        connection.macAddress = mac.text.toString().trim()
        connection.notes = notes.text.toString().trim()
        connection.favourite = favourite.isChecked
        connection.scaleFit = scale.checkedRadioButtonId == R.id.scaleFit
        connection.quality = when (quality.checkedRadioButtonId) {
            R.id.qualityBest -> SavedConnection.QUALITY_BEST
            R.id.qualityLow -> SavedConnection.QUALITY_LOW
            else -> SavedConnection.QUALITY_BALANCED
        }
        connection.viewOnly = viewOnly.isChecked
        connection.clipboard = clipboard.isChecked
        connection.lockOnDisconnect = lock.isChecked
        connection.sshTunnel = sshTunnel.isChecked
        connection.rememberPassword = rememberPassword.isChecked && store.isPasswordStoreAvailable()
        if (connection.rememberPassword) {
            store.savePassword(connection.id, password.text.toString())
        } else {
            store.deletePassword(connection.id)
        }
        store.save(connection)
        return true
    }

    private fun connect() {
        val intent = Intent(this, ViewerActivity::class.java).putExtra(ViewerActivity.EXTRA_ID, connection.id)
        // A password typed but not remembered is still used for this connection
        if (!connection.rememberPassword) intent.putExtra(ViewerActivity.EXTRA_PASSWORD, password.text.toString())
        startActivity(intent)
        finish()
    }

    private fun confirmDelete() {
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.delete_question, connection.displayName()))
            .setPositiveButton(R.string.delete) { _, _ -> store.remove(connection.id); finish() }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    companion object {
        const val EXTRA_ID = "id"
    }
}
