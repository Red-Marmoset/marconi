// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The first screen: the computers saved so far. Tap one to connect, touch and hold to change or remove it, and add
// new ones with the button at the bottom. A vnc:// or marconi:// link from another app connects straight away.

package com.redmarmoset.marconi

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.ListView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity

class ConnectActivity : AppCompatActivity() {
    private lateinit var store: SavedConnections
    private lateinit var list: ListView
    private lateinit var empty: TextView
    private lateinit var status: TextView
    private var connections: List<SavedConnection> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)
        store = SavedConnections(this)
        list = findViewById(R.id.connections)
        empty = findViewById(R.id.empty)
        status = findViewById(R.id.status)

        list.setOnItemClickListener { _, _, position, _ -> connect(connections[position]) }
        list.setOnItemLongClickListener { _, _, position, _ ->
            val connection = connections[position]
            AlertDialog.Builder(this)
                .setTitle(connection.name)
                .setItems(arrayOf(getString(R.string.edit), getString(R.string.delete))) { _, which ->
                    if (which == 0) edit(connection) else confirmDelete(connection)
                }
                .show()
            true
        }
        findViewById<Button>(R.id.add).setOnClickListener { edit(null) }

        status.text = intent?.getStringExtra(ViewerActivity.EXTRA_STATUS) ?: ""
        intent?.data?.let { openLink(it.toString()) }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        status.text = intent.getStringExtra(ViewerActivity.EXTRA_STATUS) ?: ""
        intent.data?.let { openLink(it.toString()) }
    }

    private fun refresh() {
        connections = store.all()
        list.adapter = ArrayAdapter(this, android.R.layout.simple_list_item_2, android.R.id.text1, connections).also {
            list.adapter = object : ArrayAdapter<SavedConnection>(this, android.R.layout.simple_list_item_2, android.R.id.text1, connections) {
                override fun getView(position: Int, convertView: View?, parent: android.view.ViewGroup): View {
                    val view = super.getView(position, convertView, parent)
                    view.findViewById<TextView>(android.R.id.text1).text = connections[position].name
                    view.findViewById<TextView>(android.R.id.text2).text = connections[position].subtitle()
                    return view
                }
            }
        }
        empty.visibility = if (connections.isEmpty()) View.VISIBLE else View.GONE
        list.visibility = if (connections.isEmpty()) View.GONE else View.VISIBLE
    }

    private fun edit(connection: SavedConnection?) {
        startActivity(Intent(this, EditConnectionActivity::class.java).apply {
            if (connection != null) {
                putExtra(EditConnectionActivity.EXTRA_NAME, connection.name)
                putExtra(EditConnectionActivity.EXTRA_ADDRESS, connection.address)
                putExtra(EditConnectionActivity.EXTRA_USERNAME, connection.username)
            }
        })
    }

    private fun confirmDelete(connection: SavedConnection) {
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.delete_question, connection.name))
            .setPositiveButton(R.string.delete) { _, _ -> store.delete(connection.name); refresh() }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun connect(connection: SavedConnection) {
        status.text = ""
        startActivity(Intent(this, ViewerActivity::class.java)
            .putExtra(ViewerActivity.EXTRA_ADDRESS, connection.address)
            .putExtra(ViewerActivity.EXTRA_USERNAME, connection.username)
            .putExtra(ViewerActivity.EXTRA_PASSWORD, ""))
    }

    /** A vnc:// or marconi:// link: connect to it, saving nothing. */
    private fun openLink(address: String) {
        intent.data = null
        connect(SavedConnection(address, address, ""))
    }
}
