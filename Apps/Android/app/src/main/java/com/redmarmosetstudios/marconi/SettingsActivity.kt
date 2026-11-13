// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Settings: the appearance choice, and who made Marconi.

package com.redmarmosetstudios.marconi

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButtonToggleGroup

class SettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)
        findViewById<android.view.View>(android.R.id.content).padForSystemBars()
        findViewById<MaterialToolbar>(R.id.toolbar).setNavigationOnClickListener { finish() }

        val group = findViewById<MaterialButtonToggleGroup>(R.id.appearance)
        group.check(when (Settings.appearance(this)) {
            Settings.APPEARANCE_LIGHT -> R.id.appearanceLight
            Settings.APPEARANCE_DARK -> R.id.appearanceDark
            else -> R.id.appearanceSystem
        })
        group.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (!isChecked) return@addOnButtonCheckedListener
            Settings.saveAppearance(this, when (checkedId) {
                R.id.appearanceLight -> Settings.APPEARANCE_LIGHT
                R.id.appearanceDark -> Settings.APPEARANCE_DARK
                else -> Settings.APPEARANCE_SYSTEM
            })
        }

        val version = try { packageManager.getPackageInfo(packageName, 0).versionName ?: "" } catch (e: Exception) { "" }
        findViewById<TextView>(R.id.version).text = getString(R.string.version_line, version)
    }
}
