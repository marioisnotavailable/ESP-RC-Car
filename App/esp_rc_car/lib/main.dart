import 'dart:async';
import 'dart:io'; // WebSocket (mobile/desktop)
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const RCCarApp());
}

class RCCarApp extends StatelessWidget {
  const RCCarApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'RC Car Controller',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(scaffoldBackgroundColor: Colors.black),
      home: const ControllerPage(),
    );
  }
}

class ControllerPage extends StatefulWidget {
  const ControllerPage({super.key});
  @override
  State<ControllerPage> createState() => _ControllerPageState();
}

class _ControllerPageState extends State<ControllerPage> {
  // === WS ===
  static const String wsUrl = 'ws://192.168.4.1:81/';
  WebSocket? _ws;
  Timer? _reconnectTimer;

  // === Steuergrößen ===
  static const int maxVal = 1000;
  double _throttle = 0; // Rohwerte -1000..+1000
  double _steer = 0;

  // Filter
  static const double dz = 0.08;
  static const double alphaS = 0.30;
  static const double alphaT = 0.30;
  double _steerFilt = 0;
  double _thrFilt = 0;

  // Gamepad (von Android native via EventChannel)
  static const _gamepadChannel = EventChannel('rc.gamepad/events');
  StreamSubscription? _gpSub;
  bool _haveGamepad = false;
  String _gpLabel = 'Gamepad: nicht verbunden';

  // Loop (50 Hz)
  static const int sendHz = 50;
  Timer? _loop;

  @override
  void initState() {
    super.initState();
    _connectWS();
    _subscribeGamepad();
    _startLoop();
  }

  @override
  void dispose() {
    _loop?.cancel();
    _gpSub?.cancel();
    _reconnectTimer?.cancel();
    _ws?.close();
    super.dispose();
  }

  // === WebSocket verbinden / reconnect ===
  Future<void> _connectWS() async {
    try {
      _ws = await WebSocket.connect(wsUrl, pingInterval: const Duration(seconds: 5));
      _ws?.done.whenComplete(() => _scheduleReconnect());
      setState(() {});
    } catch (_) {
      _scheduleReconnect();
    }
  }

  void _scheduleReconnect() {
    _ws = null;
    setState(() {});
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 1), _connectWS);
  }

  void _send(double thr, double st, int flags) {
    final ws = _ws;
    if (ws != null) {
      try {
        ws.add('${thr.round()},${st.round()},$flags');
      } catch (_) {
        _scheduleReconnect();
      }
    }
  }

  // === Deadzone ===
  double _applyDeadzone(double v, [double deadzone = dz]) {
    final a = v.abs();
    if (a < deadzone) return 0;
    final s = (a - deadzone) / (1 - deadzone);
    return v.isNegative ? -s : s;
  }

  // === Gamepad: Android-Stream lesen ===
  void _subscribeGamepad() {
    _gpSub = _gamepadChannel.receiveBroadcastStream().listen((event) {
      // event: { "lx": double, "r2": double, "l2": double, "connected": bool, "id": "..." }
      if (event is Map) {
        final connected = (event['connected'] as bool?) ?? false;
        _haveGamepad = connected;
        final id = (event['id'] as String?) ?? '';
        _gpLabel = connected ? 'Gamepad: verbunden ($id)' : 'Gamepad: nicht verbunden';

        // lx: -1..+1 / r2,l2: 0..1
        final lx = (event['lx'] as num?)?.toDouble() ?? 0.0;
        final r2 = ((event['r2'] as num?)?.toDouble() ?? 0.0).clamp(0.0, 1.0);
        final l2 = ((event['l2'] as num?)?.toDouble() ?? 0.0).clamp(0.0, 1.0);

        final steerAxis = _applyDeadzone(lx);
        final thrAxis = (r2 - l2).clamp(-1.0, 1.0);

        _steer = steerAxis * maxVal;
        _throttle = thrAxis * maxVal;

        if (mounted) setState(() {});
      }
    }, onError: (_) {
      _haveGamepad = false;
      _gpLabel = 'Gamepad: nicht verbunden';
      if (mounted) setState(() {});
    });
  }

  // === Loop 50 Hz ===
  void _startLoop() {
    _loop?.cancel();
    _loop = Timer.periodic(Duration(milliseconds: (1000 / sendHz).round()), (_) {
      // Nur Touch/UI (keine Keyboard-Steuerung auf Android)
      // Glättung
      _steerFilt = alphaS * _steerFilt + (1 - alphaS) * _steer;
      _thrFilt = alphaT * _thrFilt + (1 - alphaT) * _throttle;

      // Begrenzen
      _steerFilt = _steerFilt.clamp(-maxVal.toDouble(), maxVal.toDouble());
      _thrFilt = _thrFilt.clamp(-maxVal.toDouble(), maxVal.toDouble());

      // Senden
      _send(_thrFilt, _steerFilt, 0);

      if (mounted) setState(() {});
    });
  }

  @override
  Widget build(BuildContext context) {
    final w = MediaQuery.of(context).size.width;
    final stickSize = w <= 700 ? 180.0 : 220.0;
    final knobSize = w <= 700 ? 72.0 : 88.0;
    final gap = w <= 700 ? 28.0 : 48.0;

    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            const Padding(
              padding: EdgeInsets.fromLTRB(12, 18, 12, 6),
              child: Center(
                child: Text('RC Car Controller',
                    style: TextStyle(fontSize: 22, fontWeight: FontWeight.w600)),
              ),
            ),
            Expanded(
              child: Center(
                child: Wrap(
                  spacing: gap,
                  runSpacing: gap,
                  alignment: WrapAlignment.center,
                  children: [
                    Joystick(
                      size: stickSize,
                      knobSize: knobSize,
                      verticalOnly: true,
                      externalValue: Offset(0, -_thrFilt / maxVal),
                      onChanged: (o) {
                        _throttle = (-o.dy).clamp(-1.0, 1.0) * maxVal;
                      },
                      onEnd: () => _throttle = 0,
                    ),
                    Joystick(
                      size: stickSize,
                      knobSize: knobSize,
                      verticalOnly: false,
                      externalValue: Offset(_steerFilt / maxVal, 0),
                      onChanged: (o) {
                        _steer = (o.dx).clamp(-1.0, 1.0) * maxVal;
                      },
                      onEnd: () => _steer = 0,
                    ),
                  ],
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 0, 12, 18),
              child: Column(
                children: [
                  const Text(
                    'Steuerung: Touch-Sticks\n'
                    'Gamepad: linker Stick = Lenkung, R2 = vorwärts, L2 = rückwärts',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Color(0xFFA9B7C6), height: 1.45),
                  ),
                  const SizedBox(height: 6),
                  Text(_gpLabel,
                      style:
                          const TextStyle(color: Color(0xFF99AADD), fontSize: 14)),
                  const SizedBox(height: 8),
                  Text(
                    'WS: ${_ws == null ? "getrennt" : "verbunden"}'
                    ' | thr=${_thrFilt.toStringAsFixed(0)}  steer=${_steerFilt.toStringAsFixed(0)}',
                    style:
                        const TextStyle(color: Color(0xFF99AADD), fontSize: 12),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Runder Stick mit Knopf und optionaler Achsenbegrenzung.
class Joystick extends StatefulWidget {
  final double size;
  final double knobSize;
  final bool verticalOnly;
  final Offset externalValue; // -1..+1 in beiden Achsen
  final ValueChanged<Offset>? onChanged;
  final VoidCallback? onEnd;

  const Joystick({
    super.key,
    required this.size,
    required this.knobSize,
    required this.verticalOnly,
    required this.externalValue,
    this.onChanged,
    this.onEnd,
  });

  @override
  State<Joystick> createState() => _JoystickState();
}

class _JoystickState extends State<Joystick> {
  Offset _local = Offset.zero; // -1..+1
  bool _isDragging = false;

  double get _radius => widget.size / 2;
  double get _travel => widget.size * 0.36; // wie im Web (0.36*size)

  @override
  void didUpdateWidget(covariant Joystick oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!_isDragging) {
      _local = _clampNorm(widget.externalValue);
    }
  }

  @override
  Widget build(BuildContext context) {
    final knobOffset = _isDragging ? _local : _clampNorm(widget.externalValue);
    final px = knobOffset.dx * _travel;
    final py = -knobOffset.dy * _travel;

    return GestureDetector(
      onPanStart: (_) => _isDragging = true,
      onPanUpdate: (d) {
        final localPos = _positionFromRenderBox(d.localPosition, widget.size);
        var norm = Offset(localPos.dx / _radius, -localPos.dy / _radius);
        norm = widget.verticalOnly ? Offset(0, norm.dy) : Offset(norm.dx, 0);
        final clamped = widget.verticalOnly
            ? Offset(0, norm.dy.clamp(-1.0, 1.0))
            : Offset(norm.dx.clamp(-1.0, 1.0), 0);

        setState(() => _local = clamped);
        widget.onChanged?.call(_local);
      },
      onPanEnd: (_) {
        _isDragging = false;
        setState(() => _local = Offset.zero);
        widget.onEnd?.call();
      },
      child: Container(
        width: widget.size,
        height: widget.size,
        decoration: BoxDecoration(
          color: const Color(0xFF1F1F1F),
          shape: BoxShape.circle,
          boxShadow: [
            BoxShadow(
              color: Colors.white.withOpacity(0.05),
              blurRadius: 20,
              spreadRadius: -5,
            ),
          ],
        ),
        child: Center(
          child: Transform.translate(
            offset: Offset(px, py),
            child: Container(
              width: widget.knobSize,
              height: widget.knobSize,
              decoration: const BoxDecoration(
                color: Color(0xFF7A7A7A),
                shape: BoxShape.circle,
                boxShadow: [
                  BoxShadow(blurRadius: 18, offset: Offset(0, 8), color: Color(0x59000000)),
                  BoxShadow(blurRadius: 10, offset: Offset(0, 0), color: Color(0x14FFFFFF)),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Offset _positionFromRenderBox(Offset localPos, double size) {
    final center = Offset(size / 2, size / 2);
    final v = localPos - center;
    final r = _radius;
    if (v.distance <= r) return v;
    return Offset.fromDirection(v.direction, r);
  }

  Offset _clampNorm(Offset o) =>
      Offset(o.dx.clamp(-1.0, 1.0), o.dy.clamp(-1.0, 1.0));
}
