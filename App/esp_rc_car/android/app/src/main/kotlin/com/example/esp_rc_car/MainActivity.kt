package com.example.esp_rc_car

import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel

class MainActivity : FlutterActivity() {
    private val CHANNEL = "rc.gamepad/events"
    private var eventSink: EventChannel.EventSink? = null

    // State
    private var connected = false
    private var gamepadId = ""
    // Axes normalized
    private var lx = 0.0
    private var r2 = 0.0
    private var l2 = 0.0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // nothing else
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        EventChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
            .setStreamHandler(object : EventChannel.StreamHandler {
                override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                    eventSink = events
                    push()
                }
                override fun onCancel(arguments: Any?) {
                    eventSink = null
                }
            })
    }

    private fun push() {
        val map = hashMapOf(
            "connected" to connected,
            "id" to gamepadId,
            "lx" to lx,
            "r2" to r2,
            "l2" to l2
        )
        eventSink?.success(map)
    }

    private fun isGamepad(devId: Int): Boolean {
        val dev = InputDevice.getDevice(devId) ?: return false
        val sources = dev.sources
        val isJs = (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        val isGp = (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        return isJs || isGp
    }

    private fun normalizeAxis(value: Float, min: Float = -1f, max: Float = 1f): Double {
        var v = value
        if (v < min) v = min
        if (v > max) v = max
        return v.toDouble()
    }

    // Tries common mappings for triggers and left stick X.
    private fun readAxes(ev: MotionEvent) {
        // Left stick X
        val lxAxis = ev.getAxisValue(MotionEvent.AXIS_X, ev.actionIndex)
        lx = normalizeAxis(lxAxis).coerceIn(-1.0, 1.0)

        // Triggers: prefer dedicated axes if present
        var l2Raw = ev.getAxisValue(MotionEvent.AXIS_LTRIGGER, ev.actionIndex)
        var r2Raw = ev.getAxisValue(MotionEvent.AXIS_RTRIGGER, ev.actionIndex)

        // Fallbacks (some controllers map to Z / RZ or GAS/BRAKE)
        if (l2Raw == 0f) l2Raw = ev.getAxisValue(MotionEvent.AXIS_BRAKE, ev.actionIndex)
        if (r2Raw == 0f) r2Raw = ev.getAxisValue(MotionEvent.AXIS_GAS, ev.actionIndex)
        if (l2Raw == 0f) l2Raw = ev.getAxisValue(MotionEvent.AXIS_Z, ev.actionIndex) // sometimes LT
        if (r2Raw == 0f) r2Raw = ev.getAxisValue(MotionEvent.AXIS_RZ, ev.actionIndex) // sometimes RT

        // Normalisieren auf 0..1 (einige Controller liefern -1..+1)
        fun to01(x: Float): Double {
          return if (x in -1f..1f) ((x + 1f) / 2f).toDouble() else x.toDouble()
        }
        l2 = to01(l2Raw).coerceIn(0.0, 1.0)
        r2 = to01(r2Raw).coerceIn(0.0, 1.0)
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean {
        if (isGamepad(ev.deviceId)) {
            connected = true
            val dev = InputDevice.getDevice(ev.deviceId)
            gamepadId = dev?.name ?: "Gamepad"
            when (ev.action) {
                MotionEvent.ACTION_MOVE -> {
                    readAxes(ev)
                    push()
                    return true
                }
            }
        }
        return super.dispatchGenericMotionEvent(ev)
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (isGamepad(event.deviceId)) {
            connected = true
            val dev = InputDevice.getDevice(event.deviceId)
            gamepadId = dev?.name ?: "Gamepad"
            // Einige Controller liefern Trigger auch als Buttons (0..1)
            if (keyCode == KeyEvent.KEYCODE_BUTTON_L2) {
                l2 = 1.0; push(); return true
            }
            if (keyCode == KeyEvent.KEYCODE_BUTTON_R2) {
                r2 = 1.0; push(); return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (isGamepad(event.deviceId)) {
            if (keyCode == KeyEvent.KEYCODE_BUTTON_L2) {
                l2 = 0.0; push(); return true
            }
            if (keyCode == KeyEvent.KEYCODE_BUTTON_R2) {
                r2 = 0.0; push(); return true
            }
        }
        return super.onKeyUp(keyCode, event)
    }
}
