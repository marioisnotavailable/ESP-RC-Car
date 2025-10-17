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
  final double _sensitivity = 0.8; // 0.4..1.2, lower = easier to reach max
  bool _showDevPanel = false;

  @override
  void initState() {
    super.initState();
    // Try to auto-discover a WS host (non-blocking). If none found within a
    // short timeout, fall back to the configured URL.
    _autoDiscoverWS();
    _subscribeGamepad();
    _startLoop();
    // Fallback: if autosearch didn't connect within 3s, try configured URL.
    Future.delayed(const Duration(seconds: 3), () {
      if (_ws == null) _connectWS();
    });
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
  // ws connected
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
  // ws connect failed
      _scheduleReconnect();
    }
  }

  void _scheduleReconnect() {
    _ws = null;
    setState(() {});
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 1), _connectWS);
  }

  // === Auto-discovery of WS host ===
  Future<void> _autoDiscoverWS() async {
    // Derive local subnets from network interfaces and scan each /24.
    // Removed probing of common/hard-coded IPs — we only perform a full
    // /24 scan for each IPv4 interface to find a WebSocket server.
    try {
      final uri = Uri.parse(_wsUrl);
      final ifs = await NetworkInterface.list(includeLoopback: false, type: InternetAddressType.IPv4);
      const batchSize = 40; // parallelism per batch
      const tryTimeoutMs = 400; // per-attempt timeout

      for (final iface in ifs) {
        for (final addr in iface.addresses) {
          final ip = addr.address;
          final parts = ip.split('.');
          if (parts.length != 4) continue;
          final prefix = '${parts[0]}.${parts[1]}.${parts[2]}.';

          // create hosts 1..254 (skip own host)
          final hosts = <String>[];
          for (var o = 1; o <= 254; o++) {
            final h = prefix + o.toString();
            if (h == ip) continue;
            hosts.add(h);
          }

          // Scan in batches to limit parallel connections
          for (var i = 0; i < hosts.length; i += batchSize) {
            final batch = hosts.sublist(i, (i + batchSize).clamp(0, hosts.length));
            final futures = batch.map((host) async {
              final tryUrl = Uri(scheme: uri.scheme, host: host, port: uri.hasPort ? uri.port : uri.port, path: uri.path).toString();
              try {
                final sock = await WebSocket.connect(tryUrl).timeout(Duration(milliseconds: tryTimeoutMs));
                // success
                _ws = sock;
                _ws?.listen((data) {}, onDone: _scheduleReconnect, onError: (_) => _scheduleReconnect());
                if (mounted) setState(() {});
                return true;
              } catch (_) {
                return false;
              }
            }).toList();

            final results = await Future.wait(futures);
            if (results.any((r) => r)) return; // found one
          }
        }
      }
    } catch (_) {
      // ignore errors during interface listing / scanning
    }
  }

  // helper removed; scanning inlines connection attempts now

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

  // (Ping helper removed - replaced by one-shot search button)

  // One-shot search: scan local /24 subnets and return the first working ws:// URL
  Future<String?> _searchWSOnce({int batchSize = 40, int tryTimeoutMs = 400}) async {
    try {
      final uri = Uri.parse(_wsUrl);
      final ifs = await NetworkInterface.list(includeLoopback: false, type: InternetAddressType.IPv4);

      for (final iface in ifs) {
        for (final addr in iface.addresses) {
          final ip = addr.address;
          final parts = ip.split('.');
          if (parts.length != 4) continue;
          final prefix = '${parts[0]}.${parts[1]}.${parts[2]}.';

          final hosts = <String>[];
          for (var o = 1; o <= 254; o++) {
            final h = prefix + o.toString();
            if (h == ip) continue;
            hosts.add(h);
          }

          for (var i = 0; i < hosts.length; i += batchSize) {
            final batch = hosts.sublist(i, (i + batchSize).clamp(0, hosts.length));
            final futures = batch.map((host) async {
              final tryUrl = Uri(scheme: uri.scheme, host: host, port: uri.hasPort ? uri.port : uri.port, path: uri.path).toString();
              try {
                final sock = await WebSocket.connect(tryUrl).timeout(Duration(milliseconds: tryTimeoutMs));
                await sock.close();
                return tryUrl;
              } catch (_) {
                return null;
              }
            }).toList();

            final results = await Future.wait(futures);
            for (final r in results) {
              if (r != null) return r;
            }
          }
        }
      }
    } catch (_) {
      // ignore
    }
    return null;
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
            // Dev panel toggle (collapses the websocket/dev controls)
            Padding(
              padding: const EdgeInsets.fromLTRB(8, 8, 8, 6),
              child: Row(
                children: [
                  IconButton(
                    icon: Icon(_showDevPanel ? Icons.expand_less : Icons.expand_more, color: Colors.white),
                    onPressed: () => setState(() => _showDevPanel = !_showDevPanel),
                    tooltip: 'Dev panel',
                  ),
                  const SizedBox(width: 8),
                  // When expanded show the websocket input inline at the same height
                  _showDevPanel
                      ? Expanded(
                          child: Row(
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
                                onPressed: () async {
                                  // capture messenger before awaiting to avoid using BuildContext across async gaps
                                  final messenger = ScaffoldMessenger.of(context);
                                  // run a one-shot search and write the found ws url into the field
                                  final found = await _searchWSOnce();
                                  if (!mounted) return;
                                  if (found != null) {
                                    _wsUrlController.text = found;
                                    messenger.showSnackBar(SnackBar(content: Text('Found: $found')));
                                  } else {
                                    messenger.showSnackBar(const SnackBar(content: Text('No WebSocket found')));
                                  }
                                },
                                child: const Text('Search'),
                              ),
                            ],
                          ),
                        )
                      : const Spacer(),
                ],
              ),
            ),
            // Collapsible dev panel
            AnimatedCrossFade(
              firstChild: const SizedBox.shrink(),
              secondChild: const SizedBox.shrink(),
              crossFadeState: _showDevPanel ? CrossFadeState.showSecond : CrossFadeState.showFirst,
              duration: const Duration(milliseconds: 200),
            ),
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
                  // Center status between joysticks when dev panel is collapsed
                  Align(
                    alignment: const Alignment(0, 0.15),
                    child: Visibility(
                      visible: _showDevPanel,
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                        decoration: BoxDecoration(
                          color: const Color(0x15000000),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                              Text(
                                _ws == null ? 'WS: getrennt' : 'WS: verbunden',
                                style: const TextStyle(color: Color(0xFF99AADD), fontSize: 13),
                              ),
                              const SizedBox(height: 4),
                              Text(
                                'thr=${_thrFilt.toStringAsFixed(0)}  steer=${_steerFilt.toStringAsFixed(0)}',
                                style: const TextStyle(color: Color(0xFF99AADD), fontSize: 14),
                              ),
                              const SizedBox(height: 4),
                              Text(_gpLabel, style: const TextStyle(color: Color(0xFF99AADD), fontSize: 12)),
                          ],
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 6),
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
