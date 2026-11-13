// Copyright (c) 2026 Red Marmoset Studios Ltd.
// "On this network": asks the network every 10 seconds, on a thread of its own, for computers advertising screen
// sharing, Remote Desktop or SSH, and keeps a computer listed until it has missed three rounds in a row.

package com.redmarmosetstudios.marconi

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Handler
import android.os.Looper

class Discovery(context: Context, private val onChange: (List<NativeSession.Found>) -> Unit) {
    private class Entry(val found: NativeSession.Found, var missedRounds: Int = 0)

    private val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
    private val main = Handler(Looper.getMainLooper())
    private var thread: Thread? = null
    @Volatile private var running = false
    private val entries = mutableListOf<Entry>()

    fun start() {
        if (running) return
        running = true
        thread = Thread {
            // Multicast answers only reach the app while a multicast lock is held
            val lock = wifi?.createMulticastLock("marconi-discovery")?.apply { setReferenceCounted(false); acquire() }
            try {
                while (running) {
                    // A native problem must never take the app down with it: the list just stays empty
                    val found = try { NativeSession.discover(1500) } catch (e: Throwable) { emptyList() }
                    if (!running) break
                    main.post { merge(found) }
                    try { Thread.sleep(8500) } catch (e: InterruptedException) { break }
                }
            } finally {
                try { lock?.release() } catch (e: Exception) { }
            }
        }.apply { isDaemon = true; start() }
    }

    fun stop() {
        running = false
        thread?.interrupt()
        thread = null
    }

    private fun merge(found: List<NativeSession.Found>) {
        for (entry in entries) entry.missedRounds += 1
        for (service in found) {
            val existing = entries.firstOrNull { it.found.name == service.name && it.found.protocol == service.protocol }
            if (existing != null) {
                existing.missedRounds = 0
            } else {
                entries.add(Entry(service))
            }
        }
        entries.removeAll { it.missedRounds >= 3 }
        onChange(entries.map { it.found })
    }

    companion object {
        /** The address to connect to a found computer with, in Marconi's own address form. */
        fun addressOf(found: NativeSession.Found): String {
            val host = found.address.ifEmpty { found.host }
            val scheme = when (found.protocol.lowercase()) {
                "rdp" -> "rdp://"
                "ssh" -> "ssh://"
                "marconi" -> "marconi://"
                else -> "vnc://"
            }
            return if (found.port == 0) "$scheme$host" else "$scheme$host:${found.port}"
        }
    }
}
