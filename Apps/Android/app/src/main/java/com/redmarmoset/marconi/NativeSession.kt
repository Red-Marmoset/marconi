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

    private var handle: Long = create()

    /** Starts connecting; the outcome arrives through the listener. Returns a message if the address can't be used. */
    fun connect(address: String, username: String, password: String, trustedCertificateHex: String): String? =
        connect(handle, address, username, password, trustedCertificateHex)

    fun disconnect() = disconnect(handle)

    /** Frees the native session. Disconnects first; the listener gets no more callbacks afterwards. */
    fun close() {
        if (handle != 0L) {
            destroy(handle)
            handle = 0L
        }
    }

    /** Copies a rectangle of the picture into [bitmap], which must be ARGB_8888 and the picture's size. */
    fun copyFramebuffer(bitmap: Bitmap, x: Int, y: Int, width: Int, height: Int): Boolean =
        copyFramebuffer(handle, bitmap, x, y, width, height)

    fun framebufferWidth(): Int = framebufferWidth(handle)
    fun framebufferHeight(): Int = framebufferHeight(handle)

    /** Pointer in picture coordinates; buttons is the RFB mask (1 left, 2 middle, 4 right, 8 wheel up, 16 wheel down). */
    fun sendPointer(x: Int, y: Int, buttons: Int) = sendPointer(handle, x, y, buttons)
    fun sendKey(keysym: Int, down: Boolean) = sendKey(handle, keysym, down)
    fun requestFullUpdate() = requestFullUpdate(handle)
    fun isEncrypted(): Boolean = isEncrypted(handle)
    fun encryptionName(): String = encryptionName(handle)

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
    private external fun connect(handle: Long, address: String, username: String, password: String, trustedCertificateHex: String): String?
    private external fun disconnect(handle: Long)
    private external fun copyFramebuffer(handle: Long, bitmap: Bitmap, x: Int, y: Int, width: Int, height: Int): Boolean
    private external fun framebufferWidth(handle: Long): Int
    private external fun framebufferHeight(handle: Long): Int
    private external fun sendPointer(handle: Long, x: Int, y: Int, buttons: Int)
    private external fun sendKey(handle: Long, keysym: Int, down: Boolean)
    private external fun requestFullUpdate(handle: Long)
    private external fun isEncrypted(handle: Long): Boolean
    private external fun encryptionName(handle: Long): String

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

        init {
            System.loadLibrary("Marconi")
        }
    }
}
