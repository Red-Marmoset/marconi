// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The start screen, as on the desktop: a quick connect box, the saved connections (favourites and groups first)
// with search, and the computers found on this network.

package com.redmarmoset.marconi

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.widget.doAfterTextChanged
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.floatingactionbutton.ExtendedFloatingActionButton

class ConnectionsActivity : AppCompatActivity() {
    private lateinit var store: SavedConnections
    private lateinit var list: RecyclerView
    private lateinit var network: RecyclerView
    private lateinit var empty: TextView
    private lateinit var count: TextView
    private lateinit var status: TextView
    private lateinit var networkStatus: TextView
    private lateinit var search: EditText
    private lateinit var quickAddress: EditText
    private val adapter = ConnectionAdapter()
    private val networkAdapter = NetworkAdapter()
    private var connections: List<SavedConnection> = emptyList()
    private lateinit var discovery: Discovery

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connections)
        setSupportActionBar(findViewById<MaterialToolbar>(R.id.toolbar))
        supportActionBar?.setDisplayShowTitleEnabled(false)
        store = SavedConnections(this)
        list = findViewById(R.id.connections)
        network = findViewById(R.id.network)
        empty = findViewById(R.id.empty)
        count = findViewById(R.id.count)
        status = findViewById(R.id.status)
        networkStatus = findViewById(R.id.networkStatus)
        search = findViewById(R.id.search)
        quickAddress = findViewById(R.id.quickAddress)

        list.layoutManager = LinearLayoutManager(this)
        list.adapter = adapter
        network.layoutManager = LinearLayoutManager(this)
        network.adapter = networkAdapter

        findViewById<Button>(R.id.quickConnect).setOnClickListener { quickConnect() }
        quickAddress.setOnEditorActionListener { _, actionId, _ -> if (actionId == EditorInfo.IME_ACTION_GO) { quickConnect(); true } else false }
        search.doAfterTextChanged { refresh() }
        findViewById<ExtendedFloatingActionButton>(R.id.add).setOnClickListener { edit(null) }

        discovery = Discovery(this) { found ->
            networkAdapter.items = found
            networkStatus.text = getString(if (found.isEmpty()) R.string.nothing_on_network else R.string.searching_network)
            networkStatus.visibility = if (found.isEmpty()) View.VISIBLE else View.GONE
        }
        showStatus(intent)
        intent?.data?.let { openLink(it.toString()) }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.connections, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == R.id.action_settings) {
            startActivity(Intent(this, SettingsActivity::class.java))
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    override fun onResume() {
        super.onResume()
        refresh()
        discovery.start()
    }

    override fun onPause() {
        super.onPause()
        discovery.stop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        showStatus(intent)
        intent.data?.let { openLink(it.toString()) }
    }

    private fun showStatus(intent: Intent?) {
        val text = intent?.getStringExtra(ViewerActivity.EXTRA_STATUS) ?: ""
        status.text = text
        status.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
    }

    /** The list: favourites first, then each group, then the rest, filtered by the search box. */
    private fun refresh() {
        val all = store.loadAll()
        val filter = search.text.toString().trim().lowercase()
        connections = if (filter.isEmpty()) all else all.filter {
            it.displayName().lowercase().contains(filter) || it.address.lowercase().contains(filter) ||
                it.group.lowercase().contains(filter) || it.notes.lowercase().contains(filter)
        }
        count.text = getString(R.string.connections_count, all.size)
        val rows = mutableListOf<Row>()
        val favourites = connections.filter { it.favourite }
        if (favourites.isNotEmpty()) {
            rows.add(Row.Header(getString(R.string.favourites)))
            favourites.forEach { rows.add(Row.Item(it)) }
        }
        val rest = connections.filter { !it.favourite }
        val groups = rest.map { it.group }.filter { it.isNotEmpty() }.distinct().sortedBy { it.lowercase() }
        for (group in groups) {
            rows.add(Row.Header(group))
            rest.filter { it.group == group }.forEach { rows.add(Row.Item(it)) }
        }
        val ungrouped = rest.filter { it.group.isEmpty() }
        if (ungrouped.isNotEmpty()) {
            if (rows.isNotEmpty()) rows.add(Row.Header(getString(R.string.other_connections)))
            ungrouped.forEach { rows.add(Row.Item(it)) }
        }
        adapter.rows = rows
        empty.text = getString(if (all.isEmpty()) R.string.no_connections else R.string.no_matching_connections)
        empty.visibility = if (connections.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun quickConnect() {
        val address = quickAddress.text.toString().trim()
        if (address.isEmpty()) {
            quickAddress.error = getString(R.string.address_needed)
            return
        }
        // A saved connection with this address carries its own choices
        val saved = store.findByAddress(address)
        if (saved != null) connect(saved) else connectTo(address)
    }

    private fun connect(connection: SavedConnection) {
        status.visibility = View.GONE
        startActivity(Intent(this, ViewerActivity::class.java).putExtra(ViewerActivity.EXTRA_ID, connection.id))
    }

    private fun connectTo(address: String) {
        status.visibility = View.GONE
        startActivity(Intent(this, ViewerActivity::class.java).putExtra(ViewerActivity.EXTRA_ADDRESS, address))
    }

    private fun edit(connection: SavedConnection?) {
        startActivity(Intent(this, EditConnectionActivity::class.java).apply {
            if (connection != null) putExtra(EditConnectionActivity.EXTRA_ID, connection.id)
        })
    }

    private fun showActions(connection: SavedConnection) {
        val actions = mutableListOf(getString(R.string.connect), getString(R.string.edit))
        if (connection.macAddress.isNotEmpty()) actions.add(getString(R.string.wake_up))
        actions.add(getString(R.string.delete))
        AlertDialog.Builder(this)
            .setTitle(connection.displayName())
            .setItems(actions.toTypedArray()) { _, which ->
                when (actions[which]) {
                    getString(R.string.connect) -> connect(connection)
                    getString(R.string.edit) -> edit(connection)
                    getString(R.string.wake_up) -> wake(connection)
                    getString(R.string.delete) -> confirmDelete(connection)
                }
            }
            .show()
    }

    private fun wake(connection: SavedConnection) {
        Thread {
            val error = NativeSession.wake(connection.macAddress)
            runOnUiThread {
                Toast.makeText(this, if (error == null) getString(R.string.wake_sent, connection.displayName())
                                     else getString(R.string.wake_failed, error), Toast.LENGTH_SHORT).show()
            }
        }.start()
    }

    private fun confirmDelete(connection: SavedConnection) {
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.delete_question, connection.displayName()))
            .setPositiveButton(R.string.delete) { _, _ -> store.remove(connection.id); refresh() }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** A vnc:// or marconi:// link: connect to it, saving nothing. */
    private fun openLink(address: String) {
        intent.data = null
        connectTo(address)
    }

    // --- The saved list ---

    private sealed class Row {
        class Header(val title: String) : Row()
        class Item(val connection: SavedConnection) : Row()
    }

    private inner class ConnectionAdapter : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
        var rows: List<Row> = emptyList()
            set(value) { field = value; notifyDataSetChanged() }

        override fun getItemViewType(position: Int): Int = if (rows[position] is Row.Header) 0 else 1

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            val view = inflater.inflate(if (viewType == 0) R.layout.item_header else R.layout.item_connection, parent, false)
            return object : RecyclerView.ViewHolder(view) {}
        }

        override fun getItemCount(): Int = rows.size

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            when (val row = rows[position]) {
                is Row.Header -> holder.itemView.findViewById<TextView>(R.id.header).text = row.title
                is Row.Item -> {
                    val c = row.connection
                    holder.itemView.findViewById<TextView>(R.id.name).text = c.displayName()
                    holder.itemView.findViewById<TextView>(R.id.subtitle).text = c.subtitle()
                    holder.itemView.findViewById<TextView>(R.id.lastUsed).text = SavedConnections.formatLastUsed(this@ConnectionsActivity, c.lastUsed)
                    holder.itemView.findViewById<ImageView>(R.id.star).visibility = if (c.favourite) View.VISIBLE else View.INVISIBLE
                    holder.itemView.setOnClickListener { connect(c) }
                    holder.itemView.setOnLongClickListener { showActions(c); true }
                }
            }
        }
    }

    private inner class NetworkAdapter : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
        var items: List<NativeSession.Found> = emptyList()
            set(value) { field = value; notifyDataSetChanged() }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_connection, parent, false)
            return object : RecyclerView.ViewHolder(view) {}
        }

        override fun getItemCount(): Int = items.size

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            val found = items[position]
            holder.itemView.findViewById<TextView>(R.id.name).text = found.name.ifEmpty { found.host }
            holder.itemView.findViewById<TextView>(R.id.subtitle).text = found.address.ifEmpty { found.host }
            holder.itemView.findViewById<TextView>(R.id.lastUsed).text = found.protocol.uppercase()
            holder.itemView.findViewById<ImageView>(R.id.star).visibility = View.GONE
            holder.itemView.setOnClickListener { connectTo(Discovery.addressOf(found)) }
        }
    }
}
