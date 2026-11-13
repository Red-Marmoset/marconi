// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeps a screen clear of the status bar, the navigation buttons, camera cut-outs and the keyboard.

package com.redmarmosetstudios.marconi

import android.view.View
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat

/**
 * Pads this view (a screen's root) by the space the system bars, any display cut-out and the on-screen keyboard
 * take up. Apps targeting Android 15 are drawn edge to edge whatever they ask for, so without this the toolbar sits
 * under the status bar and buttons at the bottom under the navigation buttons; it also stands in for adjustResize,
 * which edge to edge ignores. On older versions the system keeps the screen clear itself and the padding is zero.
 * Bars hidden for full screen take up no space, so the padding follows them.
 */
fun View.padForSystemBars() {
    val start = intArrayOf(paddingLeft, paddingTop, paddingRight, paddingBottom)
    ViewCompat.setOnApplyWindowInsetsListener(this) { view, insets ->
        val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        val keyboard = insets.getInsets(WindowInsetsCompat.Type.ime())
        view.setPadding(start[0] + bars.left, start[1] + bars.top, start[2] + bars.right,
            start[3] + maxOf(bars.bottom, keyboard.bottom))
        WindowInsetsCompat.CONSUMED
    }
    ViewCompat.requestApplyInsets(this)
}
