// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Shows the other computer's screen and turns touches and typing into pointer and key events.
//
// Gestures: tap = click, double-tap = double-click, touch and hold = right click, double-tap and hold = drag with
// the left button, one finger moves the pointer, two fingers move and zoom the picture.

package com.redmarmoset.marconi

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Rect
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import kotlin.math.max
import kotlin.math.min

class RemoteView(context: Context, attrs: AttributeSet? = null) : View(context, attrs) {
    /** Where input goes. Coordinates are in picture pixels. */
    interface Input {
        fun pointer(x: Int, y: Int, buttons: Int)
        fun key(keysym: Int, down: Boolean)
    }

    var input: Input? = null

    /** Modifier keys held down by the key bar; applied to the next typed character and then released. */
    var stickyModifiers: MutableSet<Int> = mutableSetOf()
    var onStickyModifiersUsed: (() -> Unit)? = null

    private var bitmap: Bitmap? = null
    private val matrix = Matrix()
    private val inverse = Matrix()
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private var fitScale = 1f
    private var scale = 1f
    private var fitted = false

    private var pointerX = 0
    private var pointerY = 0
    private var buttons = 0
    private var dragging = false       // double-tap and hold: the left button stays down while the finger moves
    private var dragArmed = false      // the second tap of a double-tap is down; a move makes it a drag

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean = true

        override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
            click(e, NativeSession.BUTTON_LEFT)
            return true
        }

        override fun onDoubleTap(e: MotionEvent): Boolean {
            dragArmed = true
            moveTo(e)
            return true
        }

        override fun onDoubleTapEvent(e: MotionEvent): Boolean {
            when (e.actionMasked) {
                MotionEvent.ACTION_MOVE -> if (dragArmed && !dragging) {
                    dragging = true
                    press(NativeSession.BUTTON_LEFT)
                }
                MotionEvent.ACTION_UP -> {
                    if (dragging) {
                        moveTo(e)
                        release(NativeSession.BUTTON_LEFT)
                    } else {
                        click(e, NativeSession.BUTTON_LEFT)
                        click(e, NativeSession.BUTTON_LEFT)
                    }
                    dragging = false
                    dragArmed = false
                }
            }
            return true
        }

        override fun onLongPress(e: MotionEvent) {
            click(e, NativeSession.BUTTON_RIGHT)
        }

        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, distanceX: Float, distanceY: Float): Boolean {
            if (e2.pointerCount >= 2) {
                matrix.postTranslate(-distanceX, -distanceY)
                clampTranslation()
                invalidate()
            } else if (!dragging) {
                moveTo(e2)
            }
            return true
        }
    })

    private val scaler = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(detector: ScaleGestureDetector): Boolean {
            val wanted = (scale * detector.scaleFactor).coerceIn(fitScale, fitScale * 8f)
            val factor = wanted / scale
            matrix.postScale(factor, factor, detector.focusX, detector.focusY)
            scale = wanted
            clampTranslation()
            invalidate()
            return true
        }
    })

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    /** A new picture size: the bitmap is remade and shown whole. */
    fun setPictureSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            bitmap = null
        } else {
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        }
        fitted = false
        requestLayout()
        invalidate()
    }

    /** The bitmap to copy picture updates into; null before the first picture. */
    fun picture(): Bitmap? = bitmap

    fun invalidatePicture(x: Int, y: Int, width: Int, height: Int) {
        // The changed area in view coordinates, grown a little for the scaling
        val corners = floatArrayOf(x.toFloat(), y.toFloat(), (x + width).toFloat(), (y + height).toFloat())
        matrix.mapPoints(corners)
        invalidate(Rect(corners[0].toInt() - 1, corners[1].toInt() - 1, corners[2].toInt() + 2, corners[3].toInt() + 2))
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        fitted = false
        invalidate()
    }

    private fun fitPicture() {
        val picture = bitmap ?: return
        if (width == 0 || height == 0) return
        fitScale = min(width.toFloat() / picture.width, height.toFloat() / picture.height)
        scale = fitScale
        matrix.reset()
        matrix.postScale(fitScale, fitScale)
        matrix.postTranslate((width - picture.width * fitScale) / 2f, (height - picture.height * fitScale) / 2f)
        fitted = true
    }

    private fun clampTranslation() {
        val picture = bitmap ?: return
        val values = FloatArray(9)
        matrix.getValues(values)
        val shownWidth = picture.width * scale
        val shownHeight = picture.height * scale
        var tx = values[Matrix.MTRANS_X]
        var ty = values[Matrix.MTRANS_Y]
        tx = if (shownWidth <= width) (width - shownWidth) / 2f else tx.coerceIn(width - shownWidth, 0f)
        ty = if (shownHeight <= height) (height - shownHeight) / 2f else ty.coerceIn(height - shownHeight, 0f)
        values[Matrix.MTRANS_X] = tx
        values[Matrix.MTRANS_Y] = ty
        matrix.setValues(values)
    }

    override fun onDraw(canvas: Canvas) {
        val picture = bitmap ?: return
        if (!fitted) fitPicture()
        canvas.drawBitmap(picture, matrix, paint)
    }

    // --- Touch ---

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (bitmap == null) return false
        scaler.onTouchEvent(event)
        gestures.onTouchEvent(event)
        if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
            if (dragging) {
                release(NativeSession.BUTTON_LEFT)
            }
            dragging = false
            dragArmed = false
        }
        return true
    }

    private fun toPicture(e: MotionEvent): Pair<Int, Int>? {
        val picture = bitmap ?: return null
        if (!matrix.invert(inverse)) return null
        val point = floatArrayOf(e.x, e.y)
        inverse.mapPoints(point)
        val x = point[0].toInt().coerceIn(0, max(0, picture.width - 1))
        val y = point[1].toInt().coerceIn(0, max(0, picture.height - 1))
        return Pair(x, y)
    }

    private fun moveTo(e: MotionEvent) {
        val (x, y) = toPicture(e) ?: return
        pointerX = x
        pointerY = y
        input?.pointer(pointerX, pointerY, buttons)
    }

    private fun press(button: Int) {
        buttons = buttons or button
        input?.pointer(pointerX, pointerY, buttons)
    }

    private fun release(button: Int) {
        buttons = buttons and button.inv()
        input?.pointer(pointerX, pointerY, buttons)
    }

    private fun click(e: MotionEvent, button: Int) {
        moveTo(e)
        press(button)
        release(button)
    }

    // --- Keys ---

    fun sendTypedKeysym(keysym: Int) {
        if (keysym == 0) return
        val held = stickyModifiers.toList()
        for (modifier in held) input?.key(modifier, true)
        input?.key(keysym, true)
        input?.key(keysym, false)
        for (modifier in held.asReversed()) input?.key(modifier, false)
        if (held.isNotEmpty()) {
            stickyModifiers.clear()
            onStickyModifiersUsed?.invoke()
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyDown(keyCode, event)
        val keysym = Keys.keysymFor(event)
        if (keysym == 0) return super.onKeyDown(keyCode, event)
        if (event.repeatCount == 0 && stickyModifiers.isNotEmpty()) {
            sendTypedKeysym(keysym)
        } else {
            input?.key(keysym, true)
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyUp(keyCode, event)
        val keysym = Keys.keysymFor(event)
        if (keysym == 0) return super.onKeyUp(keyCode, event)
        input?.key(keysym, false)
        return true
    }

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        // A plain keyboard with no suggestions: typed text comes as commitText, special keys as key events
        outAttrs.inputType = EditorInfo.TYPE_CLASS_TEXT or EditorInfo.TYPE_TEXT_FLAG_NO_SUGGESTIONS or EditorInfo.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
        outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE or EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN
        return object : BaseInputConnection(this, false) {
            override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
                text?.let { typed ->
                    var i = 0
                    while (i < typed.length) {
                        val codepoint = Character.codePointAt(typed, i)
                        when (codepoint) {
                            '\n'.code -> sendTypedKeysym(Keys.RETURN)
                            '\t'.code -> sendTypedKeysym(Keys.TAB)
                            else -> sendTypedKeysym(NativeSession.keysymForCodepoint(codepoint))
                        }
                        i += Character.charCount(codepoint)
                    }
                }
                return true
            }

            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                repeat(beforeLength) { sendTypedKeysym(Keys.BACKSPACE) }
                repeat(afterLength) { sendTypedKeysym(Keys.DELETE) }
                return true
            }

            override fun sendKeyEvent(event: KeyEvent): Boolean {
                return when (event.action) {
                    KeyEvent.ACTION_DOWN -> onKeyDown(event.keyCode, event)
                    KeyEvent.ACTION_UP -> onKeyUp(event.keyCode, event)
                    else -> false
                }
            }
        }
    }
}
