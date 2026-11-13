// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Applies the saved appearance (system, light or dark) before any screen appears.

package com.redmarmoset.marconi

import android.app.Application
import androidx.appcompat.app.AppCompatDelegate

class MarconiApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        AppCompatDelegate.setDefaultNightMode(Settings.nightMode(Settings.appearance(this)))
    }
}

/** The app's own settings, kept in its preferences. */
object Settings {
    const val APPEARANCE_SYSTEM = "system"
    const val APPEARANCE_LIGHT = "light"
    const val APPEARANCE_DARK = "dark"

    fun appearance(context: android.content.Context): String =
        context.getSharedPreferences("marconi", android.content.Context.MODE_PRIVATE).getString("appearance", APPEARANCE_SYSTEM) ?: APPEARANCE_SYSTEM

    fun saveAppearance(context: android.content.Context, value: String) {
        context.getSharedPreferences("marconi", android.content.Context.MODE_PRIVATE).edit().putString("appearance", value).apply()
        AppCompatDelegate.setDefaultNightMode(nightMode(value))
    }

    fun nightMode(value: String): Int = when (value) {
        APPEARANCE_LIGHT -> AppCompatDelegate.MODE_NIGHT_NO
        APPEARANCE_DARK -> AppCompatDelegate.MODE_NIGHT_YES
        else -> AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
    }
}
