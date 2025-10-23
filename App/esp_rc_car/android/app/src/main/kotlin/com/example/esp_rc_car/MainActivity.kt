package com.example.esp_rc_car

import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel

class MainActivity : FlutterActivity() {
    private val CHANNEL = "rc.gamepad/events"
    private var eventSink: EventChannel.EventSink? = null

    // State
    private var connected = false
    private var gamepadId = ""
    // Axes normalized - NUR diese drei werden verwendet, alle anderen werden ignoriert
    private var lx = 0.0  // Nur linken Stick X für Lenkung
    private var r2 = 0.0  // Rechter Trigger für Vorwärts
    private var l2 = 0.0  // Linker Trigger für Rückwärts
    
    // Debug-Werte - werden NUR für Debug gesendet, nicht für Steuerung
    private var ry = 0.0  // Rechter Stick Y wird NICHT verwendet, nur für Debug

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
            "l2" to l2,
            "ry" to ry,  // Nur für Debug, wird in Dart nicht verwendet!
            "isRightStickActive" to (ry != 0.0)  // Flag für Debug
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

    // KOMPLETTE NEUENTWICKLUNG: Versuchen alle Controller-Achsen zu identifizieren und zu debuggen
    private fun readAxes(ev: MotionEvent) {
        // ZURÜCK ZU BASICS - Grundlegendster Ansatz, der garantiert funktioniert
        
        // Vorsicht: manche Controller haben Links/Rechts an anderen Achsen
        // Daher probieren wir verschiedene Achsen für die Lenkung
        val xAxis = ev.getAxisValue(MotionEvent.AXIS_X)
        val hatXAxis = ev.getAxisValue(MotionEvent.AXIS_HAT_X)
        
        // Die aktivere der beiden X-Achsen verwenden
        lx = if (abs(xAxis) > abs(hatXAxis)) {
            normalizeAxis(xAxis)
        } else {
            normalizeAxis(hatXAxis)
        }
        
        // 1) Nur die dedizierten Trigger-Achsen verwenden (keine Fallbacks)
        val ltrigger = ev.getAxisValue(MotionEvent.AXIS_LTRIGGER)
        val rtrigger = ev.getAxisValue(MotionEvent.AXIS_RTRIGGER)

        // 2) Rechte-Stick-Aktivität erkennen (nur zu Schutz-Zwecken)
        val rx = ev.getAxisValue(MotionEvent.AXIS_RX)
        val ryStick = ev.getAxisValue(MotionEvent.AXIS_RY)
        val hatY = ev.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val rightStickActive = (abs(rx) > 0.1f) || (abs(ryStick) > 0.1f) || (abs(hatY) > 0.1f)

        // 3) Rohwerte nur aus LTRIGGER/RTRIGGER lesen
        var l2Raw = ltrigger
        var r2Raw = rtrigger

        // 4) Sicherheit: Wenn rechter Stick aktiv ist und Trigger ~0, dann hart auf 0 setzen
        if (rightStickActive && abs(l2Raw) < 0.05f && abs(r2Raw) < 0.05f) {
            l2Raw = 0f
            r2Raw = 0f
        }

        // 5) Negative Werte auf Betrag (einige Controller nutzen -1..0)
        if (l2Raw < 0) l2Raw = abs(l2Raw)
        if (r2Raw < 0) r2Raw = abs(r2Raw)

        // 6) 0..1 normalisieren und leicht verstärken
        fun norm01(x: Float): Double = when {
            x <= 0f -> 0.0
            x >= 1f -> 1.0
            else -> x.toDouble()
        }
        l2 = (norm01(l2Raw) * 4.0).coerceIn(0.0, 1.0)
        r2 = (norm01(r2Raw) * 4.0).coerceIn(0.0, 1.0)

        // 7) Schlankes Debug
        if (l2 > 0.05 || r2 > 0.05 || rightStickActive) {
            println("🎮 T: L2=${"%.2f".format(l2)} R2=${"%.2f".format(r2)} | RS=${rightStickActive} (rx=${"%.2f".format(rx)} ry=${"%.2f".format(ryStick)})")
        }
        
        // === RECHTER STICK IDENTIFIKATION (NUR FÜR DEBUG) ===
        // Prüfen welche Achsen vom rechten Stick aktiv sein könnten
    val ryAxis = ev.getAxisValue(MotionEvent.AXIS_RY)
    val yAxis = ev.getAxisValue(MotionEvent.AXIS_Y)
    val hatYAxis = ev.getAxisValue(MotionEvent.AXIS_HAT_Y)
        
        // Die aktivste Y-Achse verwenden - aber nur für Debug-Flag
        val maxY = maxOf(abs(ryAxis), abs(yAxis), abs(hatYAxis))
        
        // WENN rechter Stick erkannt, ausführliche Debug-Info ausgeben
        if (maxY > 0.1f) {
            // ACHTUNG: Alle Y-Achsen könnten vom rechten Stick sein
            println("⛔️ RECHTER STICK AKTIV! Y=${yAxis} RY=${ryAxis} HAT_Y=${hatYAxis}")
            
            // ALLE Achsen für Problembehebung ausgeben
            println("📊 ALLE AKTIVEN ACHSEN:")
            val allAxes = mapOf(
                "AXIS_X" to ev.getAxisValue(MotionEvent.AXIS_X),
                "AXIS_Y" to ev.getAxisValue(MotionEvent.AXIS_Y),
                "AXIS_Z" to ev.getAxisValue(MotionEvent.AXIS_Z),
                "AXIS_RX" to ev.getAxisValue(MotionEvent.AXIS_RX),
                "AXIS_RY" to ev.getAxisValue(MotionEvent.AXIS_RY),
                "AXIS_RZ" to ev.getAxisValue(MotionEvent.AXIS_RZ),
                "AXIS_HAT_X" to ev.getAxisValue(MotionEvent.AXIS_HAT_X),
                "AXIS_HAT_Y" to ev.getAxisValue(MotionEvent.AXIS_HAT_Y),
                "AXIS_LTRIGGER" to ev.getAxisValue(MotionEvent.AXIS_LTRIGGER),
                "AXIS_RTRIGGER" to ev.getAxisValue(MotionEvent.AXIS_RTRIGGER),
                "AXIS_BRAKE" to ev.getAxisValue(MotionEvent.AXIS_BRAKE),
                "AXIS_GAS" to ev.getAxisValue(MotionEvent.AXIS_GAS)
            )
            allAxes.filter { (_, v) -> abs(v) > 0.1f }.forEach { (k, v) ->
                println("   $k: $v")
            }
            
            // Nur für Debug-Flag setzen, NICHT für Steuerung verwenden
            ry = normalizeAxis(maxY)
        } else {
            ry = 0.0
        }
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
