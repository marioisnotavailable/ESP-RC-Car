package com.example.esp_rc_car

import android.content.Context
import android.hardware.input.InputManager
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import kotlin.math.abs

class MainActivity : FlutterActivity(), InputManager.InputDeviceListener {
    private val GAMEPAD_CHANNEL = "rc.gamepad/events"
    private var eventSink: EventChannel.EventSink? = null
    private var inputManager: InputManager? = null

    // Gamepad state
    private var isConnected = false
    private var gamepadId = ""
    private var lx = 0.0 // Left stick X for steering (-1.0 to 1.0)
    private var r2 = 0.0 // Right trigger for throttle (0.0 to 1.0)
    private var l2 = 0.0 // Left trigger for brake/reverse (0.0 to 1.0)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        inputManager = getSystemService(Context.INPUT_SERVICE) as? InputManager
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        EventChannel(flutterEngine.dartExecutor.binaryMessenger, GAMEPAD_CHANNEL)
            .setStreamHandler(object : EventChannel.StreamHandler {
                override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                    eventSink = events
                    reportStatus()
                }

                override fun onCancel(arguments: Any?) {
                    eventSink = null
                }
            })
    }

    private fun reportStatus() {
        val payload = hashMapOf(
            "connected" to isConnected,
            "id" to gamepadId,
            "lx" to lx,
            "r2" to r2,
            "l2" to l2,
        )
        // Events must be sent on the UI thread
        runOnUiThread {
            try {
                eventSink?.success(payload)
            } catch (e: Exception) {
                // Sink may be closed, ignore
            }
        }
    }

    private fun isGamepad(device: InputDevice?): Boolean {
        if (device == null) return false
        val sources = device.sources
        // A device is considered a gamepad if it has joystick or gamepad sources.
        return (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
               (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
    }

    private fun findFirstGamepad(): InputDevice? {
        return InputDevice.getDeviceIds().mapNotNull { InputDevice.getDevice(it) }.firstOrNull { isGamepad(it) }
    }

    private fun processMotionEvent(event: MotionEvent) {
        // Steering: Use left stick X-axis. Fallback to D-pad X-axis.
        val stickX = event.getAxisValue(MotionEvent.AXIS_X)
        val dpadX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        lx = if (abs(stickX) > abs(dpadX)) stickX.toDouble() else dpadX.toDouble()

        // Throttle: Use dedicated trigger axes.
        // Some controllers report 0 to 1, others -1 to 1. Normalize to 0-1.
        val rTrigger = event.getAxisValue(MotionEvent.AXIS_RTRIGGER)
        val lTrigger = event.getAxisValue(MotionEvent.AXIS_LTRIGGER)
        
        // Fallback for controllers that use GAS/BRAKE axes
        val gas = event.getAxisValue(MotionEvent.AXIS_GAS)
        val brake = event.getAxisValue(MotionEvent.AXIS_BRAKE)

        r2 = maxOf(rTrigger, gas).coerceIn(0f, 1f).toDouble()
        l2 = maxOf(lTrigger, brake).coerceIn(0f, 1f).toDouble()

        reportStatus()
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean {
        if (isGamepad(InputDevice.getDevice(ev.deviceId))) {
            if (ev.action == MotionEvent.ACTION_MOVE) {
                processMotionEvent(ev)
                return true
            }
        }
        return super.dispatchGenericMotionEvent(ev)
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (isGamepad(InputDevice.getDevice(event.deviceId))) {
            // Handle triggers that are reported as buttons
            when (keyCode) {
                KeyEvent.KEYCODE_BUTTON_R2 -> {
                    r2 = 1.0
                    reportStatus()
                    return true
                }
                KeyEvent.KEYCODE_BUTTON_L2 -> {
                    l2 = 1.0
                    reportStatus()
                    return true
                }
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (isGamepad(InputDevice.getDevice(event.deviceId))) {
            when (keyCode) {
                KeyEvent.KEYCODE_BUTTON_R2 -> {
                    r2 = 0.0
                    reportStatus()
                    return true
                }
                KeyEvent.KEYCODE_BUTTON_L2 -> {
                    l2 = 0.0
                    reportStatus()
                    return true
                }
            }
        }
        return super.onKeyUp(keyCode, event)
    }

    private fun updateConnectionState() {
        val gamepad = findFirstGamepad()
        isConnected = gamepad != null
        gamepadId = gamepad?.name ?: ""
        if (!isConnected) {
            // Reset axes to neutral if no gamepad is connected
            lx = 0.0
            r2 = 0.0
            l2 = 0.0
        }
        reportStatus()
    }

    override fun onResume() {
        super.onResume()
        inputManager?.registerInputDeviceListener(this, null)
        updateConnectionState()
    }

    override fun onPause() {
        super.onPause()
        inputManager?.unregisterInputDeviceListener(this)
    }

    // --- InputManager.InputDeviceListener implementation ---

    override fun onInputDeviceAdded(deviceId: Int) {
        updateConnectionState()
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        updateConnectionState()
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        updateConnectionState()
    }
}
