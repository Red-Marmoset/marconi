// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The computers the user has saved: a name, an address and a user name each, kept in the app's preferences.
// Passwords are never saved; the computer asks for them when connecting.

package com.redmarmoset.marconi

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class SavedConnection(val name: String, val address: String, val username: String) {
    /** What the list shows under the name. */
    fun subtitle(): String = if (username.isEmpty()) address else "$username@$address"
}

class SavedConnections(context: Context) {
    private val preferences = context.getSharedPreferences("marconi", Context.MODE_PRIVATE)

    fun all(): List<SavedConnection> {
        val text = preferences.getString(KEY, null) ?: return emptyList()
        val array = try { JSONArray(text) } catch (e: Exception) { return emptyList() }
        val list = mutableListOf<SavedConnection>()
        for (i in 0 until array.length()) {
            val item = array.optJSONObject(i) ?: continue
            val address = item.optString("address")
            if (address.isEmpty()) continue
            list.add(SavedConnection(item.optString("name").ifEmpty { address }, address, item.optString("username")))
        }
        return list.sortedBy { it.name.lowercase() }
    }

    /** Adds a computer, or replaces the one that had [previousName]. */
    fun save(connection: SavedConnection, previousName: String? = null) {
        val others = all().filter { it.name != connection.name && it.name != previousName }
        write(others + connection)
    }

    fun delete(name: String) = write(all().filter { it.name != name })

    private fun write(connections: List<SavedConnection>) {
        val array = JSONArray()
        for (c in connections) {
            array.put(JSONObject().put("name", c.name).put("address", c.address).put("username", c.username))
        }
        preferences.edit().putString(KEY, array.toString()).apply()
    }

    companion object {
        private const val KEY = "connections"
    }
}
