// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeps a screen clear of the status bar, the navigation buttons, camera cut-outs and the keyboard.

package com.redmarmosetstudios.marconi

import android.view.View
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat

/**
 * Pads this view (a screen's root) by the space the system bars and any display cut-out take up, and by the
 * on-screen keyboard's too unless `keyboard` is false. Apps targeting Android 15 are drawn edge to edge whatever they
 * ask for, so without this the toolbar sits under the status bar and buttons at the bottom under the navigation
 * buttons; it also stands in for adjustResize, which edge to edge ignores. On older versions the system keeps the
 * screen clear itself and the padding is zero. Bars hidden for full screen take up no space, so the padding follows
 * them.
 *
 * With `keyboard` false the keyboard covers the bottom of the screen instead, and `onKeyboard` hears how much of it,
 * in pixels above the padding (0 when the keyboard is closed).
 */
fun View.padForSystemBars(keyboard: Boolean = true, onKeyboard: ((Int) -> Unit)? = null) {
    val start = intArrayOf(paddingLeft, paddingTop, paddingRight, paddingBottom)
    ViewCompat.setOnApplyWindowInsetsListener(this) { view, insets ->
        val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        val keyboardHeight = insets.getInsets(WindowInsetsCompat.Type.ime()).bottom
        val bottom = if (keyboard) maxOf(bars.bottom, keyboardHeight) else bars.bottom
        view.setPadding(start[0] + bars.left, start[1] + bars.top, start[2] + bars.right, start[3] + bottom)
        onKeyboard?.invoke(maxOf(0, keyboardHeight - bars.bottom))
        WindowInsetsCompat.CONSUMED
    }
    ViewCompat.requestApplyInsets(this)
}
