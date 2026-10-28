// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The screen of the other computer: owns the native session, keeps its picture in the RemoteView, and answers the
// session's questions (passwords, certificates, SSH keys) with dialogs while the connection thread waits.

package com.redmarmoset.marconi

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Toast
import android.widget.ToggleButton
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArraySet

class ViewerActivity : AppCompatActivity(), NativeSession.Listener, RemoteView.Input {
    private lateinit var remote: RemoteView
    private lateinit var session: NativeSession
    private var address = ""
    private var serverName = ""
    private var finishing = false

    // Questions the connection thread is waiting on; finished with "no" when the screen closes, so the thread can end
    private val pendingAnswers = CopyOnWriteArraySet<CompletableFuture<*>>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_viewer)
        remote = findViewById(R.id.remote)
        remote.input = this
        address = intent.getStringExtra(EXTRA_ADDRESS) ?: ""
        title = address

        setUpKeyBar()
        session = NativeSession(this)
        val trusted = getSharedPreferences("marconi", MODE_PRIVATE).getString("certificate:$address", "") ?: ""
        val problem = session.connect(address, intent.getStringExtra(EXTRA_USERNAME) ?: "",
            intent.getStringExtra(EXTRA_PASSWORD) ?: "", trusted)
        if (problem != null) {
            leave(problem)
        } else {
            Toast.makeText(this, getString(R.string.connecting, address), Toast.LENGTH_SHORT).show()
        }
    }

    private fun setUpKeyBar() {
        val ctrl = findViewById<ToggleButton>(R.id.keyCtrl)
        val alt = findViewById<ToggleButton>(R.id.keyAlt)
        val shift = findViewById<ToggleButton>(R.id.keyShift)
        val modifiers = mapOf(ctrl to Keys.CONTROL_L, alt to Keys.ALT_L, shift to Keys.SHIFT_L)
        for ((button, keysym) in modifiers) {
            button.setOnCheckedChangeListener { _, checked ->
                if (checked) remote.stickyModifiers.add(keysym) else remote.stickyModifiers.remove(keysym)
            }
        }
        remote.onStickyModifiersUsed = { for (button in modifiers.keys) button.isChecked = false }

        val keys = mapOf(R.id.keyEsc to Keys.ESCAPE, R.id.keyTab to Keys.TAB, R.id.keyUp to Keys.UP,
            R.id.keyDown to Keys.DOWN, R.id.keyLeft to Keys.LEFT, R.id.keyRight to Keys.RIGHT,
            R.id.keyDel to Keys.DELETE, R.id.keyWin to Keys.SUPER_L)
        for ((id, keysym) in keys) {
            findViewById<Button>(id).setOnClickListener { remote.sendTypedKeysym(keysym) }
        }
        findViewById<Button>(R.id.keyboard).setOnClickListener {
            remote.requestFocus()
            val manager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
            manager.toggleSoftInput(InputMethodManager.SHOW_IMPLICIT, 0)
        }
        findViewById<Button>(R.id.disconnect).setOnClickListener { leave(getString(R.string.disconnected)) }
    }

    override fun onDestroy() {
        finishing = true
        for (answer in pendingAnswers) answer.complete(null)
        session.close()
        super.onDestroy()
    }

    /** Back to the first screen with a line about why. */
    private fun leave(status: String) {
        if (finishing) return
        finishing = true
        startActivity(Intent(this, ConnectActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            .putExtra(EXTRA_STATUS, status))
        finish()
    }

    // --- RemoteView.Input, on the main thread ---

    override fun pointer(x: Int, y: Int, buttons: Int) = session.sendPointer(x, y, buttons)
    override fun key(keysym: Int, down: Boolean) = session.sendKey(keysym, down)

    // --- NativeSession.Listener, on the connection thread ---

    override fun onConnected(width: Int, height: Int, name: String) {
        serverName = name
        runOnUiThread {
            title = name
            remote.setPictureSize(width, height)
            val encryption = if (session.isEncrypted()) getString(R.string.encrypted, session.encryptionName())
                             else getString(R.string.not_encrypted)
            Toast.makeText(this, "$name: $encryption", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onFramebufferUpdated(x: Int, y: Int, width: Int, height: Int) {
        runOnUiThread {
            val picture = remote.picture() ?: return@runOnUiThread
            if (session.copyFramebuffer(picture, x, y, width, height)) {
                remote.invalidatePicture(x, y, width, height)
            } else {
                // The picture changed size under us: remake it and ask for all of it
                remote.setPictureSize(session.framebufferWidth(), session.framebufferHeight())
                session.requestFullUpdate()
            }
        }
    }

    override fun onDesktopResized(width: Int, height: Int) {
        runOnUiThread {
            remote.setPictureSize(width, height)
            session.requestFullUpdate()
        }
    }

    override fun onServerText(text: String) {
        runOnUiThread {
            val clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
            clipboard.setPrimaryClip(ClipData.newPlainText(serverName, text))
        }
    }

    override fun onBell() {
        runOnUiThread { remote.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS) }
    }

    override fun onDisconnected(error: String, authenticationFailed: Boolean, wasConnected: Boolean) {
        runOnUiThread {
            val status = when {
                authenticationFailed -> getString(R.string.wrong_password)
                error.isEmpty() -> getString(R.string.disconnected)
                wasConnected -> getString(R.string.connection_lost) + ": " + error
                else -> getString(R.string.connection_failed) + ": " + error
            }
            leave(status)
        }
    }

    override fun askCredentials(kind: Int, username: String): Array<String>? {
        val wantsUsername = kind != NativeSession.CREDENTIALS_PASSWORD
        return ask { answer ->
            val layout = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(48, 16, 48, 0)
            }
            val user = EditText(this).apply {
                hint = getString(R.string.username_hint)
                setText(username)
                visibility = if (wantsUsername) View.VISIBLE else View.GONE
            }
            val pass = EditText(this).apply {
                hint = getString(R.string.password_hint)
                inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            }
            layout.addView(user)
            layout.addView(pass)
            val message = when (kind) {
                NativeSession.CREDENTIALS_SSH_ACCOUNT -> R.string.credentials_ssh
                NativeSession.CREDENTIALS_PASSWORD -> R.string.credentials_password
                else -> R.string.credentials_account
            }
            AlertDialog.Builder(this)
                .setTitle(address)
                .setMessage(message)
                .setView(layout)
                .setPositiveButton(R.string.ok) { _, _ -> answer.complete(arrayOf(user.text.toString(), pass.text.toString())) }
                .setNegativeButton(R.string.cancel) { _, _ -> answer.complete(null) }
                .setOnCancelListener { answer.complete(null) }
                .show()
            pass.requestFocus()
        }
    }

    override fun askTrustCertificate(subject: String, issuer: String, validUntil: String, problem: String, fingerprint: String): Boolean {
        val trusted = ask<Boolean> { answer ->
            AlertDialog.Builder(this)
                .setTitle(R.string.trust_certificate_title)
                .setMessage(getString(R.string.trust_certificate_message, subject, issuer, validUntil, problem, fingerprint))
                .setPositiveButton(R.string.trust) { _, _ -> answer.complete(true) }
                .setNegativeButton(R.string.cancel) { _, _ -> answer.complete(false) }
                .setOnCancelListener { answer.complete(false) }
                .show()
        } ?: false
        if (trusted) {
            getSharedPreferences("marconi", MODE_PRIVATE).edit().putString("certificate:$address", fingerprint).apply()
        }
        return trusted
    }

    override fun askTrustSshHostKey(host: String, algorithm: String, fingerprint: String, previous: String): Boolean {
        val changed = if (previous.isEmpty()) "" else getString(R.string.trust_ssh_changed, previous)
        return ask<Boolean> { answer ->
            AlertDialog.Builder(this)
                .setTitle(R.string.trust_ssh_title)
                .setMessage(getString(R.string.trust_ssh_message, host, algorithm, fingerprint, changed))
                .setPositiveButton(R.string.trust) { _, _ -> answer.complete(true) }
                .setNegativeButton(R.string.cancel) { _, _ -> answer.complete(false) }
                .setOnCancelListener { answer.complete(false) }
                .show()
        } ?: false
    }

    /** Shows a dialog on the main thread and waits here, on the connection thread, for its answer. */
    private fun <T> ask(show: (CompletableFuture<T?>) -> Unit): T? {
        val answer = CompletableFuture<T?>()
        if (finishing) return null
        pendingAnswers.add(answer)
        runOnUiThread {
            if (finishing || isFinishing) answer.complete(null) else show(answer)
        }
        try {
            return answer.get()
        } finally {
            pendingAnswers.remove(answer)
        }
    }

    companion object {
        const val EXTRA_ADDRESS = "address"
        const val EXTRA_USERNAME = "username"
        const val EXTRA_PASSWORD = "password"
        const val EXTRA_STATUS = "status"
    }
}
