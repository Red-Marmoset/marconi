// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Saved connections, with the same choices the desktop app keeps: kept in the app's preferences, passwords in an
// encrypted store of their own (the phone's keystore guards its key).

package com.redmarmoset.marconi

import android.content.Context
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.json.JSONArray
import org.json.JSONObject
import java.text.DateFormat
import java.util.Calendar
import java.util.Date
import java.util.UUID

data class SavedConnection(
    val id: String = UUID.randomUUID().toString(),
    var name: String = "",
    var address: String = "",
    var username: String = "",
    var rememberPassword: Boolean = false,
    var group: String = "",
    var notes: String = "",
    var macAddress: String = "",
    var favourite: Boolean = false,
    var scaleFit: Boolean = true,          // else actual size (100%)
    var quality: Int = QUALITY_BALANCED,
    var viewOnly: Boolean = false,
    var clipboard: Boolean = true,
    var lockOnDisconnect: Boolean = false,
    var sshTunnel: Boolean = false,
    var certificate: String = "",          // SHA-256 of a server certificate the user chose to trust, as hex
    var lastUsed: Long = 0L,               // milliseconds since 1970; 0 = never
) {
    fun displayName(): String = name.ifEmpty { address }

    /** What the list shows under the name. */
    fun subtitle(): String = if (username.isEmpty()) address else "$username@$address"

    companion object {
        const val QUALITY_BEST = 0
        const val QUALITY_BALANCED = 1
        const val QUALITY_LOW = 2

        fun fromJson(item: JSONObject): SavedConnection = SavedConnection(
            id = item.optString("id").ifEmpty { UUID.randomUUID().toString() },
            name = item.optString("name"),
            address = item.optString("address"),
            username = item.optString("username"),
            rememberPassword = item.optBoolean("rememberPassword"),
            group = item.optString("group"),
            notes = item.optString("notes"),
            macAddress = item.optString("macAddress"),
            favourite = item.optBoolean("favourite"),
            scaleFit = item.optBoolean("scaleFit", true),
            quality = item.optInt("quality", QUALITY_BALANCED),
            viewOnly = item.optBoolean("viewOnly"),
            clipboard = item.optBoolean("clipboard", true),
            lockOnDisconnect = item.optBoolean("lockOnDisconnect"),
            sshTunnel = item.optBoolean("sshTunnel"),
            certificate = item.optString("certificate"),
            lastUsed = item.optLong("lastUsed"),
        )
    }

    fun toJson(): JSONObject = JSONObject()
        .put("id", id).put("name", name).put("address", address).put("username", username)
        .put("rememberPassword", rememberPassword).put("group", group).put("notes", notes)
        .put("macAddress", macAddress).put("favourite", favourite).put("scaleFit", scaleFit)
        .put("quality", quality).put("viewOnly", viewOnly).put("clipboard", clipboard)
        .put("lockOnDisconnect", lockOnDisconnect).put("sshTunnel", sshTunnel)
        .put("certificate", certificate).put("lastUsed", lastUsed)
}

class SavedConnections(private val context: Context) {
    private val preferences = context.getSharedPreferences("marconi", Context.MODE_PRIVATE)

    /** Most recently used first. */
    fun loadAll(): List<SavedConnection> {
        val text = preferences.getString(KEY, null) ?: return emptyList()
        val array = try { JSONArray(text) } catch (e: Exception) { return emptyList() }
        val list = mutableListOf<SavedConnection>()
        for (i in 0 until array.length()) {
            val item = array.optJSONObject(i) ?: continue
            if (item.optString("address").isEmpty()) continue
            list.add(SavedConnection.fromJson(item))
        }
        return list.sortedWith(compareByDescending<SavedConnection> { it.lastUsed }.thenBy { it.displayName().lowercase() })
    }

    fun load(id: String): SavedConnection? = loadAll().firstOrNull { it.id == id }

    /** The saved connection with this address, if any (case doesn't matter). */
    fun findByAddress(address: String): SavedConnection? = loadAll().firstOrNull { it.address.equals(address, ignoreCase = true) }

    /** Adds or updates. */
    fun save(connection: SavedConnection) = write(loadAll().filter { it.id != connection.id } + connection)

    fun remove(id: String) {
        write(loadAll().filter { it.id != id })
        deletePassword(id)
    }

    fun markUsed(id: String) {
        val connection = load(id) ?: return
        connection.lastUsed = System.currentTimeMillis()
        save(connection)
    }

    fun groups(): List<String> = loadAll().map { it.group }.filter { it.isNotEmpty() }.distinct().sortedBy { it.lowercase() }

    private fun write(connections: List<SavedConnection>) {
        val array = JSONArray()
        for (c in connections) array.put(c.toJson())
        preferences.edit().putString(KEY, array.toString()).apply()
    }

    // --- Passwords ---

    private val passwords by lazy {
        try {
            val key = MasterKey.Builder(context).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build()
            EncryptedSharedPreferences.create(context, "marconi-passwords", key,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM)
        } catch (e: Exception) {
            null
        }
    }

    fun isPasswordStoreAvailable(): Boolean = passwords != null
    fun loadPassword(id: String): String? = passwords?.getString(id, null)
    fun savePassword(id: String, password: String): Boolean {
        val store = passwords ?: return false
        store.edit().putString(id, password).apply()
        return true
    }
    fun deletePassword(id: String) { passwords?.edit()?.remove(id)?.apply() }

    companion object {
        private const val KEY = "connections"

        /** "Never", "Today 14:05", "Yesterday 09:30" or the date, as the desktop list shows it. */
        fun formatLastUsed(context: Context, millis: Long): String {
            if (millis == 0L) return context.getString(R.string.last_used_never)
            val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(millis))
            val today = Calendar.getInstance().apply { set(Calendar.HOUR_OF_DAY, 0); set(Calendar.MINUTE, 0); set(Calendar.SECOND, 0); set(Calendar.MILLISECOND, 0) }
            val yesterday = (today.clone() as Calendar).apply { add(Calendar.DAY_OF_YEAR, -1) }
            return when {
                millis >= today.timeInMillis -> context.getString(R.string.last_used_today, time)
                millis >= yesterday.timeInMillis -> context.getString(R.string.last_used_yesterday, time)
                else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(millis))
            }
        }
    }
}
