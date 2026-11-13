// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Shows the other computer's screen (or one of its displays) and turns touches and typing into pointer and key
// events, with the desktop's zoom levels.
//
// Gestures: tap = click, double-tap = double-click, touch and hold = right click, double-tap and hold = drag with
// the left button, one finger moves the pointer, two fingers move and zoom the picture, and scroll the other
// computer's window while the picture fits the screen. With view only on, one finger moves the picture.

package com.redmarmoset.marconi

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

class RemoteView(context: Context, attrs: AttributeSet? = null) : View(context, attrs) {
    /** Where input goes. Coordinates are in picture pixels. */
    interface Input {
        fun pointer(x: Int, y: Int, buttons: Int)
        fun key(keysym: Int, down: Boolean)
    }

    var input: Input? = null

    /** Just watch: nothing is sent, and one finger moves the picture instead of the pointer. */
    var viewOnly = false

    /** Modifier keys held down by the key bar; applied to the next typed character and then released. */
    var stickyModifiers: MutableSet<Int> = mutableSetOf()
    var onStickyModifiersUsed: (() -> Unit)? = null

    private var bitmap: Bitmap? = null
    private var visible = Rect()       // the part of the picture shown: one display, or all of it
    private val matrix = Matrix()
    private val inverse = Matrix()
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private var fitScale = 1f
    private var scale = 1f
    private var fitted = false
    private var wantedZoomPercent = 0   // 0 = fit the screen; else a zoom level to apply once the view has a size

    private var pointerX = 0
    private var pointerY = 0
    private var buttons = 0
    private var dragging = false       // double-tap and hold: the left button stays down while the finger moves
    private var dragArmed = false      // the second tap of a double-tap is down; a move makes it a drag
    private var wheelDistance = 0f     // two-finger movement gathered towards the next wheel click

    /** The zoom as a percentage of the picture's own size (100 = one picture pixel per screen pixel). */
    val zoomPercent: Int get() = (scale * 100f).roundToInt()
    val isFitted: Boolean get() = wantedZoomPercent == 0 && abs(scale - fitScale) < 0.001f

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean = true

        override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
            if (!viewOnly) click(e, NativeSession.BUTTON_LEFT)
            return true
        }

        override fun onDoubleTap(e: MotionEvent): Boolean {
            if (viewOnly) {
                // Double-tap toggles between fitting the screen and actual size, as a photo viewer does
                if (isFitted) setZoomPercent(100, e.x, e.y) else fitToScreen()
                return true
            }
            dragArmed = true
            moveTo(e)
            return true
        }

        override fun onDoubleTapEvent(e: MotionEvent): Boolean {
            if (viewOnly) return true
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
            if (!viewOnly) click(e, NativeSession.BUTTON_RIGHT)
        }

        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, distanceX: Float, distanceY: Float): Boolean {
            if (e2.pointerCount >= 2) {
                if (isFitted && !viewOnly) {
                    scrollWheel(distanceY)
                } else {
                    matrix.postTranslate(-distanceX, -distanceY)
                    clampTranslation()
                    invalidate()
                }
            } else if (viewOnly) {
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
            applyScale(scale * detector.scaleFactor, detector.focusX, detector.focusY)
            wantedZoomPercent = if (abs(scale - fitScale) < 0.001f) 0 else zoomPercent
            return true
        }
    })

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    /** A new picture size: the bitmap is remade and shown whole. */
    fun setPictureSize(width: Int, height: Int) {
        bitmap = if (width <= 0 || height <= 0) null else Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        visible = Rect(0, 0, max(0, width), max(0, height))
        fitted = false
        requestLayout()
        invalidate()
    }

    /** Shows one display of the picture, or all of it with null. */
    fun showDisplay(rect: Rect?) {
        val picture = bitmap ?: return
        visible = rect?.let { Rect(it).apply { intersect(0, 0, picture.width, picture.height) } }
            ?: Rect(0, 0, picture.width, picture.height)
        fitted = false
        invalidate()
    }

    /** The bitmap to copy picture updates into; null before the first picture. */
    fun picture(): Bitmap? = bitmap

    fun invalidatePicture(x: Int, y: Int, width: Int, height: Int) {
        val corners = floatArrayOf(x.toFloat(), y.toFloat(), (x + width).toFloat(), (y + height).toFloat())
        matrix.mapPoints(corners)
        invalidate(Rect(corners[0].toInt() - 1, corners[1].toInt() - 1, corners[2].toInt() + 2, corners[3].toInt() + 2))
    }

    fun fitToScreen() {
        wantedZoomPercent = 0
        fitted = false
        invalidate()
    }

    /** One of the desktop's zoom levels, kept around the given point of the view (its centre by default). */
    fun setZoomPercent(percent: Int, focusX: Float = width / 2f, focusY: Float = height / 2f) {
        wantedZoomPercent = percent
        if (!fitted) fitPicture()
        applyScale(percent / 100f, focusX, focusY)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        fitted = false
        invalidate()
    }

    private fun fitPicture() {
        if (bitmap == null || visible.isEmpty || width == 0 || height == 0) return
        fitScale = min(width.toFloat() / visible.width(), height.toFloat() / visible.height())
        scale = fitScale
        matrix.reset()
        matrix.postTranslate(-visible.left.toFloat(), -visible.top.toFloat())
        matrix.postScale(fitScale, fitScale)
        matrix.postTranslate((width - visible.width() * fitScale) / 2f, (height - visible.height() * fitScale) / 2f)
        fitted = true
        if (wantedZoomPercent != 0) applyScale(wantedZoomPercent / 100f, width / 2f, height / 2f)
    }

    private fun applyScale(wanted: Float, focusX: Float, focusY: Float) {
        val target = wanted.coerceIn(min(fitScale, 0.25f), max(fitScale, 8f))
        val factor = target / scale
        matrix.postScale(factor, factor, focusX, focusY)
        scale = target
        clampTranslation()
        invalidate()
    }

    private fun clampTranslation() {
        if (visible.isEmpty) return
        val bounds = RectF(visible)
        matrix.mapRect(bounds)
        var dx = 0f
        var dy = 0f
        if (bounds.width() <= width) dx = (width - bounds.width()) / 2f - bounds.left
        else if (bounds.left > 0) dx = -bounds.left
        else if (bounds.right < width) dx = width - bounds.right
        if (bounds.height() <= height) dy = (height - bounds.height()) / 2f - bounds.top
        else if (bounds.top > 0) dy = -bounds.top
        else if (bounds.bottom < height) dy = height - bounds.bottom
        matrix.postTranslate(dx, dy)
    }

    override fun onDraw(canvas: Canvas) {
        val picture = bitmap ?: return
        if (!fitted) fitPicture()
        canvas.save()
        canvas.concat(matrix)
        canvas.clipRect(visible)
        canvas.drawBitmap(picture, 0f, 0f, paint)
        canvas.restore()
    }

    // --- Touch ---

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (bitmap == null) return false
        scaler.onTouchEvent(event)
        gestures.onTouchEvent(event)
        if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
            if (dragging) release(NativeSession.BUTTON_LEFT)
            dragging = false
            dragArmed = false
            wheelDistance = 0f
        }
        return true
    }

    private fun toPicture(e: MotionEvent): Pair<Int, Int>? {
        if (bitmap == null || !matrix.invert(inverse)) return null
        val point = floatArrayOf(e.x, e.y)
        inverse.mapPoints(point)
        val x = point[0].toInt().coerceIn(visible.left, max(visible.left, visible.right - 1))
        val y = point[1].toInt().coerceIn(visible.top, max(visible.top, visible.bottom - 1))
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

    /** Two fingers moving over a fitted picture: a wheel click for every 40 screen pixels, the way content follows the fingers. */
    private fun scrollWheel(distanceY: Float) {
        wheelDistance += distanceY
        val step = 40f * resources.displayMetrics.density / 2.5f
        while (abs(wheelDistance) >= step) {
            val button = if (wheelDistance < 0) NativeSession.BUTTON_WHEEL_UP else NativeSession.BUTTON_WHEEL_DOWN
            press(button)
            release(button)
            wheelDistance += if (wheelDistance < 0) step else -step
        }
    }

    // --- Keys ---

    fun sendTypedKeysym(keysym: Int) {
        if (keysym == 0 || viewOnly) return
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
        if (keyCode == KeyEvent.KEYCODE_BACK || viewOnly) return super.onKeyDown(keyCode, event)
        val keysym = Keys.keysymFor(event)
        if (keysym == 0) return super.onKeyDown(keyCode, event)
        if (event.repeatCount == 0 && stickyModifiers.isNotEmpty()) sendTypedKeysym(keysym) else input?.key(keysym, true)
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK || viewOnly) return super.onKeyUp(keyCode, event)
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

            override fun sendKeyEvent(event: KeyEvent): Boolean = when (event.action) {
                KeyEvent.ACTION_DOWN -> onKeyDown(event.keyCode, event)
                KeyEvent.ACTION_UP -> onKeyUp(event.keyCode, event)
                else -> false
            }
        }
    }
}
