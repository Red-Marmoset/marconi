// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The screen of the other computer, with the desktop viewer's menu: zoom, displays, picture quality, view only,
// the clipboard both ways, sent keys, screenshots. Owns the native session, keeps its picture in the RemoteView,
// and answers the session's questions (passwords, certificates, SSH keys) with dialogs while the connection
// thread waits.

package com.redmarmosetstudios.marconi

import android.content.ClipData
import android.content.ClipboardManager
import android.content.ContentValues
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.view.HapticFeedbackConstants
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.Toast
import android.widget.ToggleButton
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.floatingactionbutton.FloatingActionButton
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArraySet

class ViewerActivity : AppCompatActivity(), NativeSession.Listener, RemoteView.Input {
    private lateinit var store: SavedConnections
    private lateinit var connection: SavedConnection
    private lateinit var remote: RemoteView
    private lateinit var toolbar: MaterialToolbar
    private lateinit var keyBar: View
    private lateinit var menuButton: FloatingActionButton
    private lateinit var session: NativeSession
    private lateinit var clipboard: ClipboardManager
    private var serverName = ""
    private var connected = false
    private var finishing = false
    private var fullScreen = false
    private var shareClipboard = true
    private var quality = SavedConnection.QUALITY_BALANCED
    private var currentDisplay = 0            // a display id, or 0 for all
    private var lastClipboardSent = ""
    private var lastClipboardReceived = ""
    private val main = Handler(Looper.getMainLooper())

    // Questions the connection thread is waiting on; finished with "no" when the screen closes, so the thread can end
    private val pendingAnswers = CopyOnWriteArraySet<CompletableFuture<*>>()

    private val clipboardListener = ClipboardManager.OnPrimaryClipChangedListener { sendClipboardIfShared() }

    private val pinger = object : Runnable {
        override fun run() {
            if (connected) {
                session.sendPing()
                updateSubtitle()
            }
            main.postDelayed(this, 3000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_viewer)
        WindowCompat.setDecorFitsSystemWindows(window, true)
        store = SavedConnections(this)
        remote = findViewById(R.id.remote)
        toolbar = findViewById(R.id.toolbar)
        keyBar = findViewById(R.id.keyBar)
        menuButton = findViewById(R.id.menuButton)
        clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
        remote.input = this

        // A saved connection brings its choices; a quick connect uses the defaults
        val saved = intent.getStringExtra(EXTRA_ID)?.let { store.load(it) }
        connection = saved ?: SavedConnection(address = intent.getStringExtra(EXTRA_ADDRESS) ?: "",
                                              username = intent.getStringExtra(EXTRA_USERNAME) ?: "")
        var password = intent.getStringExtra(EXTRA_PASSWORD) ?: ""
        if (saved != null) {
            store.markUsed(saved.id)
            if (saved.rememberPassword) password = store.loadPassword(saved.id) ?: password
        }
        remote.viewOnly = connection.viewOnly
        shareClipboard = connection.clipboard
        quality = connection.quality

        toolbar.title = connection.displayName()
        toolbar.inflateMenu(R.menu.viewer)
        toolbar.setOnMenuItemClickListener { onMenuItem(it) }
        toolbar.menu.findItem(R.id.menu_keyboard).setOnMenuItemClickListener { showKeyboard(); true }
        menuButton.setOnClickListener { button ->
            PopupMenu(this, button).apply {
                menuInflater.inflate(R.menu.viewer, menu)
                prepareMenu(menu)
                setOnMenuItemClickListener { onMenuItem(it) }
            }.show()
        }
        setUpKeyBar()

        session = NativeSession(this)
        val trusted = if (saved != null) saved.certificate
                      else getSharedPreferences("marconi", MODE_PRIVATE).getString("certificate:${connection.address}", "") ?: ""
        val problem = session.connect(connection.address, connection.username, password, trusted, connection.sshTunnel, quality)
        if (problem != null) {
            leave(problem)
        } else {
            toolbar.subtitle = getString(R.string.connecting, connection.address)
        }
    }

    override fun onResume() {
        super.onResume()
        clipboard.addPrimaryClipChangedListener(clipboardListener)
        main.post { sendClipboardIfShared() } // what was copied while another app was in front
        main.removeCallbacks(pinger)
        main.postDelayed(pinger, 3000)
    }

    override fun onPause() {
        super.onPause()
        clipboard.removePrimaryClipChangedListener(clipboardListener)
        main.removeCallbacks(pinger)
    }

    override fun onDestroy() {
        finishing = true
        for (answer in pendingAnswers) answer.complete(null)
        session.close()
        super.onDestroy()
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
            R.id.keyDel to Keys.DELETE, R.id.keyWin to Keys.SUPER_L, R.id.keyF8 to Keys.F1 + 7)
        for ((id, keysym) in keys) findViewById<Button>(id).setOnClickListener { remote.sendTypedKeysym(keysym) }
    }

    private fun showKeyboard() {
        remote.requestFocus()
        (getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager).toggleSoftInput(InputMethodManager.SHOW_IMPLICIT, 0)
    }

    // --- The menu ---

    private fun prepareMenu(menu: Menu) {
        val zoomIds = mapOf(50 to R.id.zoom_50, 75 to R.id.zoom_75, 100 to R.id.zoom_100, 125 to R.id.zoom_125,
                            150 to R.id.zoom_150, 200 to R.id.zoom_200)
        menu.findItem(R.id.zoom_fit)?.isChecked = remote.isFitted
        for ((percent, id) in zoomIds) menu.findItem(id)?.isChecked = !remote.isFitted && remote.zoomPercent == percent
        menu.findItem(R.id.menu_full_screen)?.isChecked = fullScreen
        menu.findItem(R.id.menu_view_only)?.isChecked = remote.viewOnly
        menu.findItem(R.id.menu_share_clipboard)?.isChecked = shareClipboard
        menu.findItem(R.id.menu_show_keys)?.isChecked = keyBar.visibility == View.VISIBLE
        menu.findItem(R.id.menu_match_size)?.isVisible = session.supportsDesktopResize()
        menu.findItem(when (quality) {
            SavedConnection.QUALITY_BEST -> R.id.quality_best
            SavedConnection.QUALITY_LOW -> R.id.quality_low
            else -> R.id.quality_balanced
        })?.isChecked = true
        // One entry per display the server describes
        val display = menu.findItem(R.id.menu_display)
        val screens = session.screens()
        display?.isVisible = screens.size > 1
        display?.subMenu?.let { sub ->
            for (i in sub.size() - 1 downTo 0) if (sub.getItem(i).itemId != R.id.display_all) sub.removeItem(sub.getItem(i).itemId)
            sub.findItem(R.id.display_all)?.isChecked = currentDisplay == 0
            screens.forEachIndexed { index, screen ->
                val item = sub.add(R.id.display_group, DISPLAY_ITEM_BASE + screen.id, Menu.NONE, getString(R.string.menu_display_n, index + 1))
                item.isCheckable = true
                item.isChecked = currentDisplay == screen.id
            }
            sub.setGroupCheckable(R.id.display_group, true, true)
        }
    }

    override fun onPrepareOptionsMenu(menu: Menu): Boolean {
        prepareMenu(toolbar.menu)
        return super.onPrepareOptionsMenu(menu)
    }

    private fun onMenuItem(item: MenuItem): Boolean {
        prepareMenu(toolbar.menu)
        when (item.itemId) {
            R.id.menu_keyboard -> showKeyboard()
            R.id.zoom_fit -> remote.fitToScreen()
            R.id.zoom_50 -> remote.setZoomPercent(50)
            R.id.zoom_75 -> remote.setZoomPercent(75)
            R.id.zoom_100 -> remote.setZoomPercent(100)
            R.id.zoom_125 -> remote.setZoomPercent(125)
            R.id.zoom_150 -> remote.setZoomPercent(150)
            R.id.zoom_200 -> remote.setZoomPercent(200)
            R.id.display_all -> { currentDisplay = 0; remote.showDisplay(null) }
            R.id.quality_best -> setQuality(SavedConnection.QUALITY_BEST)
            R.id.quality_balanced -> setQuality(SavedConnection.QUALITY_BALANCED)
            R.id.quality_low -> setQuality(SavedConnection.QUALITY_LOW)
            R.id.menu_match_size -> matchSize()
            R.id.menu_full_screen -> setFullScreen(!fullScreen)
            R.id.menu_view_only -> remote.viewOnly = !remote.viewOnly
            R.id.menu_share_clipboard -> { shareClipboard = !shareClipboard; if (shareClipboard) sendClipboardIfShared() }
            R.id.menu_send_clipboard -> sendClipboard(true)
            R.id.menu_get_clipboard -> session.requestServerClipboard()
            R.id.keys_ctrl_alt_del -> sendCombination(Keys.CONTROL_L, Keys.ALT_L, Keys.DELETE)
            R.id.keys_alt_tab -> sendCombination(Keys.ALT_L, Keys.TAB)
            R.id.keys_alt_f4 -> sendCombination(Keys.ALT_L, Keys.F1 + 3)
            R.id.keys_windows -> sendCombination(Keys.SUPER_L)
            R.id.keys_ctrl_esc -> sendCombination(Keys.CONTROL_L, Keys.ESCAPE)
            R.id.keys_ctrl_shift_esc -> sendCombination(Keys.CONTROL_L, Keys.SHIFT_L, Keys.ESCAPE)
            R.id.keys_print_screen -> sendCombination(Keys.PRINT)
            R.id.keys_cmd_tab -> sendCombination(Keys.SUPER_L, Keys.TAB)
            R.id.keys_f8 -> sendCombination(Keys.F1 + 7)
            R.id.keys_f9 -> sendCombination(Keys.F1 + 8)
            R.id.keys_f10 -> sendCombination(Keys.F1 + 9)
            R.id.screenshot_save -> saveScreenshot()
            R.id.screenshot_share -> shareScreenshot()
            R.id.menu_show_keys -> keyBar.visibility = if (keyBar.visibility == View.VISIBLE) View.GONE else View.VISIBLE
            R.id.menu_refresh -> session.requestFullUpdate()
            R.id.menu_disconnect -> leave(getString(R.string.disconnected))
            else -> {
                if (item.itemId >= DISPLAY_ITEM_BASE) {
                    val screen = session.screens().firstOrNull { it.id == item.itemId - DISPLAY_ITEM_BASE } ?: return true
                    currentDisplay = screen.id
                    remote.showDisplay(Rect(screen.x, screen.y, screen.x + screen.width, screen.y + screen.height))
                } else return false
            }
        }
        return true
    }

    private fun setQuality(value: Int) {
        quality = value
        session.setQuality(value)
    }

    private fun matchSize() {
        if (!session.supportsDesktopResize()) {
            Toast.makeText(this, R.string.resize_refused, Toast.LENGTH_SHORT).show()
            return
        }
        // The other computer's screen becomes this view's size, so it fits at actual size. RDP and some VNC servers
        // want a width that is a multiple of 4.
        session.requestDesktopSize((remote.width and 3.inv()).coerceIn(640, 8192), remote.height.coerceIn(480, 8192))
    }

    private fun setFullScreen(on: Boolean) {
        fullScreen = on
        val controller = WindowInsetsControllerCompat(window, remote)
        if (on) {
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        } else {
            controller.show(WindowInsetsCompat.Type.systemBars())
        }
        toolbar.visibility = if (on) View.GONE else View.VISIBLE
        menuButton.visibility = if (on) View.VISIBLE else View.GONE
    }

    /** A combination pressed and released, as the desktop's Send Keys menu does it. */
    private fun sendCombination(vararg keysyms: Int) {
        if (remote.viewOnly) return
        for (keysym in keysyms) session.sendKey(keysym, true)
        for (keysym in keysyms.reversed()) session.sendKey(keysym, false)
    }

    // --- The clipboard ---

    private fun sendClipboardIfShared() {
        if (shareClipboard && connected) sendClipboard(false)
    }

    private fun sendClipboard(byHand: Boolean) {
        val text = clipboard.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(this)?.toString() ?: ""
        if (text.isEmpty()) {
            if (byHand) Toast.makeText(this, R.string.clipboard_empty, Toast.LENGTH_SHORT).show()
            return
        }
        if (!byHand && (text == lastClipboardSent || text == lastClipboardReceived)) return
        if (session.sendClipboardText(text)) {
            lastClipboardSent = text
            if (byHand) Toast.makeText(this, R.string.clipboard_sent, Toast.LENGTH_SHORT).show()
        }
    }

    // --- Screenshots ---

    private fun screenshot(): Bitmap? = remote.picture()?.copy(Bitmap.Config.ARGB_8888, false)

    private fun saveScreenshot() {
        val picture = screenshot() ?: return
        val fileName = "Marconi ${connection.displayName()} ${System.currentTimeMillis() / 1000}.png"
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val values = ContentValues().apply {
                    put(MediaStore.Images.Media.DISPLAY_NAME, fileName)
                    put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                    put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/Marconi")
                }
                val uri = contentResolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values) ?: throw IllegalStateException()
                contentResolver.openOutputStream(uri)!!.use { picture.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } else {
                val folder = File(getExternalFilesDir(Environment.DIRECTORY_PICTURES), "Marconi").apply { mkdirs() }
                FileOutputStream(File(folder, fileName)).use { picture.compress(Bitmap.CompressFormat.PNG, 100, it) }
            }
            Toast.makeText(this, R.string.screenshot_saved, Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, R.string.screenshot_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun shareScreenshot() {
        val picture = screenshot() ?: return
        try {
            val folder = File(cacheDir, "screenshots").apply { mkdirs() }
            val file = File(folder, "Marconi screenshot.png")
            FileOutputStream(file).use { picture.compress(Bitmap.CompressFormat.PNG, 100, it) }
            val uri = FileProvider.getUriForFile(this, "com.redmarmosetstudios.marconi.files", file)
            startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
                type = "image/png"
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }, getString(R.string.menu_screenshot)))
        } catch (e: Exception) {
            Toast.makeText(this, R.string.screenshot_failed, Toast.LENGTH_SHORT).show()
        }
    }

    // --- Leaving ---

    /** Back to the first screen with a line about why. */
    private fun leave(status: String) {
        if (finishing) return
        finishing = true
        if (connection.lockOnDisconnect && connected && !remote.viewOnly) {
            // Win+L on Windows and most Linux desktops; Ctrl+Cmd+Q on a Mac (Command is Super over VNC)
            if (session.isAppleServer()) sendCombinationNow(Keys.CONTROL_L, Keys.SUPER_L, 'q'.code)
            else sendCombinationNow(Keys.SUPER_L, 'l'.code)
        }
        startActivity(Intent(this, ConnectionsActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            .putExtra(EXTRA_STATUS, status))
        finish()
    }

    private fun sendCombinationNow(vararg keysyms: Int) {
        for (keysym in keysyms) session.sendKey(keysym, true)
        for (keysym in keysyms.reversed()) session.sendKey(keysym, false)
    }

    private fun updateSubtitle() {
        val encryption = if (session.isEncrypted()) getString(R.string.encrypted, session.encryptionName()) else getString(R.string.not_encrypted)
        val roundTrip = session.roundTripMs()
        toolbar.subtitle = if (roundTrip >= 0) "$encryption · ${getString(R.string.round_trip, roundTrip)}" else encryption
    }

    // --- RemoteView.Input, on the main thread ---

    override fun pointer(x: Int, y: Int, buttons: Int) { if (!remote.viewOnly) session.sendPointer(x, y, buttons) }
    override fun key(keysym: Int, down: Boolean) { if (!remote.viewOnly) session.sendKey(keysym, down) }

    // --- NativeSession.Listener, on the connection thread ---

    override fun onConnected(width: Int, height: Int, name: String) {
        serverName = name
        runOnUiThread {
            connected = true
            toolbar.title = if (connection.name.isEmpty()) name else connection.displayName()
            remote.setPictureSize(width, height)
            if (!connection.scaleFit) remote.setZoomPercent(100)
            updateSubtitle()
            sendClipboardIfShared()
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
            currentDisplay = 0
            session.requestFullUpdate()
        }
    }

    override fun onServerText(text: String) {
        runOnUiThread {
            if (!shareClipboard) return@runOnUiThread
            lastClipboardReceived = text
            clipboard.setPrimaryClip(ClipData.newPlainText(serverName, text))
            Toast.makeText(this, R.string.clipboard_received, Toast.LENGTH_SHORT).show()
        }
    }

    override fun onBell() {
        runOnUiThread { remote.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS) }
    }

    override fun onDisconnected(error: String, authenticationFailed: Boolean, wasConnected: Boolean) {
        runOnUiThread {
            connected = false
            leave(when {
                authenticationFailed -> getString(R.string.wrong_password)
                error.isEmpty() -> getString(R.string.disconnected)
                wasConnected -> getString(R.string.connection_lost) + ": " + error
                else -> getString(R.string.connection_failed) + ": " + error
            })
        }
    }

    override fun askCredentials(kind: Int, username: String): Array<String>? {
        val wantsUsername = kind != NativeSession.CREDENTIALS_PASSWORD
        return ask { answer ->
            val layout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(48, 16, 48, 0) }
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
                .setTitle(connection.displayName())
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
            val saved = store.load(connection.id)
            if (saved != null) {
                saved.certificate = fingerprint
                store.save(saved)
            } else {
                getSharedPreferences("marconi", MODE_PRIVATE).edit().putString("certificate:${connection.address}", fingerprint).apply()
            }
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
        runOnUiThread { if (finishing || isFinishing) answer.complete(null) else show(answer) }
        try {
            return answer.get()
        } finally {
            pendingAnswers.remove(answer)
        }
    }

    companion object {
        const val EXTRA_ID = "id"
        const val EXTRA_ADDRESS = "address"
        const val EXTRA_USERNAME = "username"
        const val EXTRA_PASSWORD = "password"
        const val EXTRA_STATUS = "status"
        private const val DISPLAY_ITEM_BASE = 10000
    }
}
