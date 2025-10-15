import 'dart:async';
import 'dart:io'; // WebSocket (mobile/desktop)
// removed unused import

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // Force landscape orientation for easier side-by-side (seitwärts) control
  SystemChrome.setPreferredOrientations([
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]).then((_) {
    runApp(const RCCarApp());
  });
}

class RCCarApp extends StatelessWidget {
  const RCCarApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: '',
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
  // Editable WebSocket URL to help debugging on different networks/devices
  final TextEditingController _wsUrlController =
      TextEditingController(text: 'ws://192.168.4.1:81/');
  String get _wsUrl => _wsUrlController.text;
  WebSocket? _ws;
  Timer? _reconnectTimer;
  String _wsInfo = '';

  // === Steuergrößen ===
  static const int maxVal = 1000;
  double _throttle = 0; // Rohwerte -1000..+1000
  double _steer = 0;
  // debug fields removed

  // Filter
  static const double dz = 0.08;
  static const double alphaS = 0.30;
  static const double alphaT = 0.30;
  double _steerFilt = 0;
  double _thrFilt = 0;

  // Gamepad (von Android native via EventChannel)
  static const _gamepadChannel = EventChannel('rc.gamepad/events');
  StreamSubscription? _gpSub;
  String _gpLabel = 'Gamepad: nicht verbunden';

  // Loop (50 Hz)
  static const int sendHz = 50;
  Timer? _loop;
  double _sensitivity = 0.8; // 0.4..1.2, lower = easier to reach max

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
    _wsUrlController.dispose();
    super.dispose();
  }

  // === WebSocket verbinden / reconnect ===
  Future<void> _connectWS() async {
    try {
  debugPrint('[WS] connecting to $_wsUrl');
  _ws = await WebSocket.connect(_wsUrl).timeout(const Duration(seconds: 5));
      debugPrint('[WS] connected');
      _wsInfo = 'verbunden';
      _ws?.listen((data) {
        debugPrint('[WS] recv: $data');
      }, onError: (e) {
        debugPrint('[WS] error: $e');
      }, onDone: () {
        debugPrint('[WS] done');
        _scheduleReconnect();
      });
      _ws?.pingInterval = const Duration(seconds: 10);
      setState(() {});
    } catch (e) {
      debugPrint('[WS] connect failed: $e');
      _wsInfo = 'getrennt';
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
        final msg = '${thr.round()},${st.round()},$flags';
        ws.add(msg);
        debugPrint('[WS] send: $msg');
      } catch (_) {
        _scheduleReconnect();
      }
    }
  }

  // Simple HTTP GET to the WS host (helps verify network reachability)
  Future<void> _pingHost() async {
    try {
      final uri = Uri.parse(_wsUrl);
      final scheme = uri.scheme == 'ws' ? 'http' : (uri.scheme == 'wss' ? 'https' : uri.scheme);
      final hostUri = Uri(scheme: scheme, host: uri.host, port: uri.hasPort ? uri.port : null, path: '/');
      debugPrint('[PING] GET $hostUri');
      final client = HttpClient();
      client.connectionTimeout = const Duration(seconds: 3);
      final req = await client.getUrl(hostUri).timeout(const Duration(seconds: 4));
      final resp = await req.close().timeout(const Duration(seconds: 4));
      debugPrint('[PING] status=${resp.statusCode}');
      _wsInfo = 'ping ${resp.statusCode}';
      client.close(force: true);
    } catch (e) {
      debugPrint('[PING] failed: $e');
      _wsInfo = 'ping failed';
    }
    if (mounted) setState(() {});
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
  // connected state is shown via _gpLabel; no separate flag needed
        final id = (event['id'] as String?) ?? '';
        _gpLabel = connected ? 'Gamepad: verbunden ($id)' : 'Gamepad: nicht verbunden';

        // lx: -1..+1 / r2,l2: 0..1
        final lx = (event['lx'] as num?)?.toDouble() ?? 0.0;
        final r2 = ((event['r2'] as num?)?.toDouble() ?? 0.0).clamp(0.0, 1.0);
        final l2 = ((event['l2'] as num?)?.toDouble() ?? 0.0).clamp(0.0, 1.0);

  final steerAxis = _applyDeadzone(lx);
  final thrAxis = ((r2 - l2)).clamp(-1.0, 1.0);

  // Debug: ensure we see full trigger values
  debugPrint('[GP] id=$id r2=$r2 l2=$l2 thrAxis=$thrAxis');

  _steer = steerAxis * maxVal;
  _throttle = thrAxis * maxVal;

        if (mounted) setState(() {});
      }
    }, onError: (_) {
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
  final stickSize = w <= 700 ? 240.0 : 320.0; // slightly smaller
  final knobSize = w <= 700 ? 100.0 : 120.0;
  // gap was previously used by the Wrap layout; kept here for reference if needed
  // final gap = w <= 700 ? 28.0 : 48.0;

    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            // header removed per user request
            // Main control area: pin joysticks to left and right edges for better reach
            Expanded(
              child: Stack(
                children: [
                  // Left joystick (throttle) aligned to left edge and slightly above bottom
                  Align(
                    alignment: Alignment(-0.98, 0.6), // further out to the left
                    child: _EdgeStickyJoystick(
                      stickSize: stickSize,
                      knobSize: knobSize,
                      verticalOnly: true,
                      sensitivity: _sensitivity,
                      // push UP => positive throttle, so don't invert here
                      externalValue: Offset(0, _thrFilt / maxVal),
                      onChanged: (o) {
                        // Ensure UP -> +maxVal
                        setState(() {
                          _throttle = (o.dy).clamp(-1.0, 1.0) * maxVal;
                        });
                      },
                      onEnd: () => _throttle = 0,
                    ),
                  ),
                  // Right joystick (steer) aligned to right edge and slightly above bottom
                  Align(
                    alignment: Alignment(0.98, 0.6), // further out to the right
                    child: _EdgeStickyJoystick(
                      stickSize: stickSize,
                      knobSize: knobSize,
                      verticalOnly: false,
                      sensitivity: _sensitivity,
                      externalValue: Offset(_steerFilt / maxVal, 0),
                      onChanged: (o) {
                        setState(() {
                          _steer = (o.dx).clamp(-1.0, 1.0) * maxVal;
                        });
                      },
                      onEnd: () => _steer = 0,
                    ),
                  ),
                  // debug overlay removed
                ],
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 0, 12, 18),
              child: Column(
                children: [
                  const SizedBox.shrink(),
                  const SizedBox(height: 6),
                  Text(_gpLabel,
                      style:
                          const TextStyle(color: Color(0xFF99AADD), fontSize: 14)),
                  const SizedBox(height: 8),
                  Text(
                    'WS: ${_ws == null ? _wsInfo == '' ? "getrennt" : _wsInfo : "verbunden"}'
                    ' | thr=${_thrFilt.toStringAsFixed(0)}  steer=${_steerFilt.toStringAsFixed(0)}',
                    style:
                        const TextStyle(color: Color(0xFF99AADD), fontSize: 12),
                  ),
                  const SizedBox(height: 8),
                  Row(
                    children: [
                      Expanded(
                        child: TextField(
                          controller: _wsUrlController,
                          style: const TextStyle(color: Color(0xFF99AADD)),
                          decoration: const InputDecoration(
                            hintText: 'ws://192.168.4.1:81/',
                            hintStyle: TextStyle(color: Color(0xFF6E7A8A)),
                            isDense: true,
                            contentPadding: EdgeInsets.symmetric(horizontal: 8, vertical: 8),
                            border: OutlineInputBorder(),
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      ElevatedButton(
                        onPressed: () {
                          _reconnectTimer?.cancel();
                          _ws?.close();
                          _connectWS();
                        },
                        child: const Text('Connect'),
                      ),
                      const SizedBox(width: 8),
                      ElevatedButton(
                        onPressed: () {
                          _pingHost();
                        },
                        child: const Text('Ping'),
                      ),
                      const SizedBox(width: 12),
                      SizedBox(
                        width: 180,
                        child: Row(
                          children: [
                            const Text('Sens', style: TextStyle(color: Color(0xFF99AADD))),
                            Expanded(
                              child: Slider(
                                value: _sensitivity,
                                min: 0.4,
                                max: 1.2,
                                divisions: 8,
                                label: _sensitivity.toStringAsFixed(2),
                                onChanged: (v) {
                                  setState(() => _sensitivity = v);
                                },
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
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

/// A wrapper that increases the hit area and limits the visible joystick size so
/// the knob remains reachable when pinned to the screen edge.
class _EdgeStickyJoystick extends StatelessWidget {
  final double stickSize;
  final double knobSize;
  final bool verticalOnly;
  final double sensitivity;
  
  final Offset externalValue;
  final ValueChanged<Offset>? onChanged;
  final VoidCallback? onEnd;

  const _EdgeStickyJoystick({
    required this.stickSize,
    required this.knobSize,
    required this.verticalOnly,
    required this.externalValue,
    required this.sensitivity,
    this.onChanged,
    this.onEnd,
  });

  @override
  Widget build(BuildContext context) {
    // Make a larger invisible hit area so the user can reach the stick closer to
    // the screen edge. The visible circular joystick remains the same size.
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 6.0),
      child: SizedBox(
        width: stickSize * 1.05,
        height: stickSize * 1.05,
        child: Center(
          child: Joystick(
            size: stickSize,
            knobSize: knobSize,
            verticalOnly: verticalOnly,
            externalValue: externalValue,
            sensitivity: sensitivity,
            onChanged: onChanged,
            onEnd: onEnd,
          ),
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
  final double sensitivity;
  final ValueChanged<Offset>? onChanged;
  final VoidCallback? onEnd;

  const Joystick({
    super.key,
    required this.size,
    required this.knobSize,
    required this.verticalOnly,
    required this.externalValue,
    this.sensitivity = 0.8,
    this.onChanged,
    this.onEnd,
  });

  @override
  State<Joystick> createState() => _JoystickState();
}

class _JoystickState extends State<Joystick> {
  Offset _local = Offset.zero; // -1..+1
  bool _isDragging = false;

  // Sensitivity: lower => easier to reach max with less travel (0.0..1.0)
  // 0.6 means only 60% of the physical radius is needed to reach normalized 1.0
  double _sensitivity = 0.5;
  Offset? _pointerOffset;

  @override
  void initState() {
    super.initState();
    _sensitivity = widget.sensitivity;
  }

  double get _radius => widget.size / 2;
  double get _travel => widget.size * 0.42; // visual travel for knob

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
      onPanDown: (DragDownDetails details) {
        _isDragging = true;
        // compute pointer offset so the knob won't jump to finger
        final p = details.localPosition;
        final center = Offset(widget.size / 2, widget.size / 2);
        final knobCenter = center + Offset(_local.dx * _travel, -_local.dy * _travel);
        _pointerOffset = p - knobCenter;
      },
      onPanUpdate: (d) {
        final p = d.localPosition;
        final desiredKnobCenter = p - (_pointerOffset ?? Offset.zero);
        final center = Offset(widget.size / 2, widget.size / 2);
        final v = desiredKnobCenter - center;
        var norm = Offset(v.dx / (_radius * _sensitivity), -v.dy / (_radius * _sensitivity));
        norm = widget.verticalOnly ? Offset(0, norm.dy) : Offset(norm.dx, 0);
        final clamped = widget.verticalOnly
            ? Offset(0, norm.dy.clamp(-1.0, 1.0))
            : Offset(norm.dx.clamp(-1.0, 1.0), 0);

        setState(() => _local = clamped);
        widget.onChanged?.call(_local);
        // Debug raw joystick values
        // Note: this prints a lot when dragging; useful for diagnosing range issues
        debugPrint('[JS] knob dx=${_local.dx.toStringAsFixed(3)} dy=${_local.dy.toStringAsFixed(3)}');
      },
      onPanEnd: (_) {
        _isDragging = false;
        _pointerOffset = null;
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
              color: Color(0x0DFFFFFF),
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

  Offset _clampNorm(Offset o) =>
      Offset(o.dx.clamp(-1.0, 1.0), o.dy.clamp(-1.0, 1.0));
}
