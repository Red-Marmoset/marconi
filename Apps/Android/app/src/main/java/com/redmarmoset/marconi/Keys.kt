// Copyright (c) 2026 Red Marmoset Studios Ltd.
// X11 keysyms for the keys the app sends (the values in MarconiCore/Keysyms.h), and the map from Android key codes.

package com.redmarmoset.marconi

import android.view.KeyEvent

object Keys {
    const val BACKSPACE = 0xff08
    const val TAB = 0xff09
    const val RETURN = 0xff0d
    const val ESCAPE = 0xff1b
    const val DELETE = 0xffff
    const val HOME = 0xff50
    const val LEFT = 0xff51
    const val UP = 0xff52
    const val RIGHT = 0xff53
    const val DOWN = 0xff54
    const val PAGE_UP = 0xff55
    const val PAGE_DOWN = 0xff56
    const val END = 0xff57
    const val INSERT = 0xff63
    const val F1 = 0xffbe
    const val SHIFT_L = 0xffe1
    const val CONTROL_L = 0xffe3
    const val ALT_L = 0xffe9
    const val SUPER_L = 0xffeb

    /** The keysym for a hardware or soft-keyboard key event, or 0 when it has none. */
    fun keysymFor(event: KeyEvent): Int {
        val special = when (event.keyCode) {
            KeyEvent.KEYCODE_DEL -> BACKSPACE
            KeyEvent.KEYCODE_FORWARD_DEL -> DELETE
            KeyEvent.KEYCODE_TAB -> TAB
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> RETURN
            KeyEvent.KEYCODE_ESCAPE -> ESCAPE
            KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
            KeyEvent.KEYCODE_DPAD_UP -> UP
            KeyEvent.KEYCODE_DPAD_DOWN -> DOWN
            KeyEvent.KEYCODE_MOVE_HOME -> HOME
            KeyEvent.KEYCODE_MOVE_END -> END
            KeyEvent.KEYCODE_PAGE_UP -> PAGE_UP
            KeyEvent.KEYCODE_PAGE_DOWN -> PAGE_DOWN
            KeyEvent.KEYCODE_INSERT -> INSERT
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> SHIFT_L
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> CONTROL_L
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> ALT_L
            KeyEvent.KEYCODE_META_LEFT, KeyEvent.KEYCODE_META_RIGHT -> SUPER_L
            in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> F1 + (event.keyCode - KeyEvent.KEYCODE_F1)
            else -> 0
        }
        if (special != 0) return special
        // Ctrl+letter arrives without a character: send the letter itself, the server applies the modifier
        val plain = event.getUnicodeChar(event.metaState and (KeyEvent.META_SHIFT_MASK or KeyEvent.META_CAPS_LOCK_ON))
        return if (plain != 0) NativeSession.keysymForCodepoint(plain) else 0
    }
}
