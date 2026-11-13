// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One connection to another computer, run by the native core (Modules/MarconiAndroid). The listener's callbacks
// arrive on the connection thread; the three ask* methods block that thread until the app answers.

package com.redmarmoset.marconi

import android.graphics.Bitmap

class NativeSession(private val listener: Listener) {
    interface Listener {
        fun onConnected(width: Int, height: Int, name: String)
        fun onFramebufferUpdated(x: Int, y: Int, width: Int, height: Int)
        fun onDesktopResized(width: Int, height: Int)
        fun onServerText(text: String)
        fun onBell()
        fun onDisconnected(error: String, authenticationFailed: Boolean, wasConnected: Boolean)
        /** Blocking: [user name, password], or null to cancel the connection. */
        fun askCredentials(kind: Int, username: String): Array<String>?
        /** Blocking: whether to connect to a server whose certificate this phone doesn't trust. */
        fun askTrustCertificate(subject: String, issuer: String, validUntil: String, problem: String, fingerprint: String): Boolean
        /** Blocking: whether to trust an SSH server key not trusted before. */
        fun askTrustSshHostKey(host: String, algorithm: String, fingerprint: String, previous: String): Boolean
    }

    /** One of the server's displays, as it sits in the whole picture. */
    data class Screen(val id: Int, val x: Int, val y: Int, val width: Int, val height: Int)

    /** A computer found on the network. */
    data class Found(val name: String, val host: String, val address: String, val port: Int, val protocol: String)

    private var handle: Long = create()

    // Every call checks the handle: a picture update queued on the main thread can arrive after close().

    /** Starts connecting; the outcome arrives through the listener. Returns a message if the address can't be used. */
    fun connect(address: String, username: String, password: String, trustedCertificateHex: String,
                sshTunnel: Boolean, quality: Int): String? =
        if (handle == 0L) "The session is closed."
        else connect(handle, address, username, password, trustedCertificateHex, sshTunnel, quality, "Marconi for Android")

    fun disconnect() { if (handle != 0L) disconnect(handle) }

    /** Frees the native session. Disconnects first; the listener gets no more callbacks afterwards. */
    fun close() {
        if (handle != 0L) {
            destroy(handle)
            handle = 0L
        }
    }

    /** Copies a rectangle of the picture into [bitmap], which must be ARGB_8888 and the picture's size. */
    fun copyFramebuffer(bitmap: Bitmap, x: Int, y: Int, width: Int, height: Int): Boolean =
        handle != 0L && copyFramebuffer(handle, bitmap, x, y, width, height)

    fun framebufferWidth(): Int = if (handle == 0L) 0 else framebufferWidth(handle)
    fun framebufferHeight(): Int = if (handle == 0L) 0 else framebufferHeight(handle)

    /** Pointer in picture coordinates; buttons is the RFB mask (1 left, 2 middle, 4 right, 8 wheel up, 16 wheel down). */
    fun sendPointer(x: Int, y: Int, buttons: Int) { if (handle != 0L) sendPointer(handle, x, y, buttons) }
    fun sendKey(keysym: Int, down: Boolean) { if (handle != 0L) sendKey(handle, keysym, down) }
    fun requestFullUpdate() { if (handle != 0L) requestFullUpdate(handle) }
    fun isEncrypted(): Boolean = handle != 0L && isEncrypted(handle)
    fun encryptionName(): String = if (handle == 0L) "" else encryptionName(handle)
    fun setQuality(quality: Int) { if (handle != 0L) setQuality(handle, quality) }
    fun sendClipboardText(text: String): Boolean = handle != 0L && sendClipboardText(handle, text)
    fun requestServerClipboard(): Boolean = handle != 0L && requestServerClipboard(handle)
    fun supportsDesktopResize(): Boolean = handle != 0L && supportsDesktopResize(handle)
    fun requestDesktopSize(width: Int, height: Int) { if (handle != 0L) requestDesktopSize(handle, width, height) }
    fun roundTripMs(): Int = if (handle == 0L) -1 else roundTripMs(handle)
    fun sendPing() { if (handle != 0L) sendPing(handle) }
    fun isAppleServer(): Boolean = handle != 0L && isAppleServer(handle)

    fun screens(): List<Screen> {
        if (handle == 0L) return emptyList()
        val values = screens(handle)
        return (values.indices step 5).map { Screen(values[it], values[it + 1], values[it + 2], values[it + 3], values[it + 4]) }
    }

    // Called from the native side, on the connection thread.
    @Suppress("unused") private fun onConnected(width: Int, height: Int, name: String) = listener.onConnected(width, height, name)
    @Suppress("unused") private fun onFramebufferUpdated(x: Int, y: Int, width: Int, height: Int) = listener.onFramebufferUpdated(x, y, width, height)
    @Suppress("unused") private fun onDesktopResized(width: Int, height: Int) = listener.onDesktopResized(width, height)
    @Suppress("unused") private fun onServerText(text: String) = listener.onServerText(text)
    @Suppress("unused") private fun onBell() = listener.onBell()
    @Suppress("unused") private fun onDisconnected(error: String, authenticationFailed: Boolean, wasConnected: Boolean) =
        listener.onDisconnected(error, authenticationFailed, wasConnected)
    @Suppress("unused") private fun askCredentials(kind: Int, username: String): Array<String>? = listener.askCredentials(kind, username)
    @Suppress("unused") private fun askTrustCertificate(subject: String, issuer: String, validUntil: String, problem: String, fingerprint: String): Boolean =
        listener.askTrustCertificate(subject, issuer, validUntil, problem, fingerprint)
    @Suppress("unused") private fun askTrustSshHostKey(host: String, algorithm: String, fingerprint: String, previous: String): Boolean =
        listener.askTrustSshHostKey(host, algorithm, fingerprint, previous)

    private external fun create(): Long
    private external fun destroy(handle: Long)
    private external fun connect(handle: Long, address: String, username: String, password: String, trustedCertificateHex: String,
                                 sshTunnel: Boolean, quality: Int, clientName: String): String?
    private external fun disconnect(handle: Long)
    private external fun copyFramebuffer(handle: Long, bitmap: Bitmap, x: Int, y: Int, width: Int, height: Int): Boolean
    private external fun framebufferWidth(handle: Long): Int
    private external fun framebufferHeight(handle: Long): Int
    private external fun sendPointer(handle: Long, x: Int, y: Int, buttons: Int)
    private external fun sendKey(handle: Long, keysym: Int, down: Boolean)
    private external fun requestFullUpdate(handle: Long)
    private external fun isEncrypted(handle: Long): Boolean
    private external fun encryptionName(handle: Long): String
    private external fun setQuality(handle: Long, quality: Int)
    private external fun sendClipboardText(handle: Long, text: String): Boolean
    private external fun requestServerClipboard(handle: Long): Boolean
    private external fun screens(handle: Long): IntArray
    private external fun supportsDesktopResize(handle: Long): Boolean
    private external fun requestDesktopSize(handle: Long, width: Int, height: Int)
    private external fun roundTripMs(handle: Long): Int
    private external fun sendPing(handle: Long)
    private external fun isAppleServer(handle: Long): Boolean

    companion object {
        // Rfb::CredentialsKind
        const val CREDENTIALS_PASSWORD = 0
        const val CREDENTIALS_ACCOUNT = 1
        const val CREDENTIALS_PASSWORD_OR_ACCOUNT = 2
        const val CREDENTIALS_SSH_ACCOUNT = 3

        const val BUTTON_LEFT = 1
        const val BUTTON_MIDDLE = 2
        const val BUTTON_RIGHT = 4
        const val BUTTON_WHEEL_UP = 8
        const val BUTTON_WHEEL_DOWN = 16

        /** The keysym for a Unicode character (Marconi::Keysym::FromUnicode); 0 for control characters. */
        @JvmStatic external fun keysymForCodepoint(codepoint: Int): Int

        /** Wakes a computer by its hardware address; returns a message when the call couldn't be sent. */
        @JvmStatic external fun wake(macAddress: String): String?

        /** Computers advertising screen sharing, Remote Desktop or SSH on this network. Blocks for [listenMs]. */
        fun discover(listenMs: Int): List<Found> = discoverNative(listenMs).mapNotNull { line ->
            val parts = line.split('\t')
            if (parts.size < 5) null else Found(parts[0], parts[1], parts[2], parts[3].toIntOrNull() ?: 0, parts[4])
        }

        @JvmStatic private external fun discoverNative(listenMs: Int): Array<String>

        init {
            System.loadLibrary("Marconi")
        }
    }
}
