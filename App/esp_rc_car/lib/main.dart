import 'dart:async';
import 'dart:io'; // WebSocket (mobile/desktop)
// removed unused import 'dart:math'
// removed unused import

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
// Note: we avoid platform plugins here to keep discovery portable without extra entitlements

// === Subnet scanning helpers (top-level) ===
class _Subnet {
  final int network; // IPv4 as int
  final int mask; // mask as int
  final int prefix; // CIDR prefix length (e.g., 24)
  final String selfIp; // the device IP inside this subnet
  _Subnet(this.network, this.mask, this.prefix, this.selfIp);

  bool contains(String ipStr) {
    final ip = _ipv4ToInt(ipStr);
    return (ip & mask) == network;
  }

  int get firstHost {
    return network | 1; // skip network address
  }

  int get lastHost {
    final bcast = network | (~mask & 0xFFFFFFFF);
    return bcast - 1; // skip broadcast
  }
}

int _ipv4ToInt(String ip) {
  final p = ip.split('.').map(int.parse).toList();
  return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

String _intToIpv4(int x) {
  final a = (x >> 24) & 0xFF;
  final b = (x >> 16) & 0xFF;
  final c = (x >> 8) & 0xFF;
  final d = x & 0xFF;
  return '$a.$b.$c.$d';
}


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
  
  // === Gamepad Kalibrierung ===
  // Speichert die maximal beobachteten Werte für die Kalibrierung
  double _maxObservedR2 = 0.1; // Startwert um Division durch 0 zu vermeiden
  double _maxObservedL2 = 0.1; // Startwert um Division durch 0 zu vermeiden

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
    // Always scan the entire local subnet(s) to find the websocket host.
    try {
      // 1) Try UDP (fast path)
      final udpFound = await _udpDiscoverWS(timeoutMs: 1500);
      if (udpFound) return;
      // 2) Fallback: TCP unicast sweep (no broadcast/multicast needed)
      if (_ws == null) {
        final tcpFound = await _tcpDiscoverWS(timeoutMs: 600);
        if (tcpFound) return;
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

  // One-shot search: UDP discovery only, return the first announced ws:// URL
  Future<String?> _searchWSOnce({int tryTimeoutMs = 1000}) async {
    // Try UDP first (fast path)
    try {
      final udpUrl = await _udpDiscoverUrl(timeoutMs: tryTimeoutMs);
      if (udpUrl != null) return udpUrl;
    } catch (_) {}
    // Fallback: quick TCP port scan (unicast), no multicast/broadcast required
    try {
      final tcpUrl = await _tcpDiscoverUrl(timeoutMs: tryTimeoutMs);
      if (tcpUrl != null) return tcpUrl;
    } catch (_) {}
    return null;
  }

  // === UDP discovery (matches ESP responder/beacon) ===
  static const int _discPort = 49352;
  static const String _discQuery = 'ESP_RC_DISCOVER';
  static const String _discRespPrefix = 'ESP_RC_HERE ';

  Future<bool> _udpDiscoverWS({int timeoutMs = 1200}) async {
    final url = await _udpDiscoverUrl(timeoutMs: timeoutMs);
    if (url == null) return false;
    try {
      final sock = await WebSocket.connect(url).timeout(Duration(milliseconds: timeoutMs));
      _ws = sock;
      _wsUrlController.text = url;
      _ws?.listen((data) {}, onDone: _scheduleReconnect, onError: (_) => _scheduleReconnect());
      if (mounted) setState(() {});
      debugPrint('[WS] UDP discovered and connected: $url');
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<bool> _tcpDiscoverWS({int timeoutMs = 1200}) async {
    final url = await _tcpDiscoverUrl(timeoutMs: timeoutMs);
    if (url == null) return false;
    try {
      final sock = await WebSocket.connect(url).timeout(Duration(milliseconds: timeoutMs));
      _ws = sock;
      _wsUrlController.text = url;
      _ws?.listen((data) {}, onDone: _scheduleReconnect, onError: (_) => _scheduleReconnect());
      if (mounted) setState(() {});
      debugPrint('[WS] TCP discovered and connected: $url');
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<String?> _udpDiscoverUrl({int timeoutMs = 1200}) async {
    RawDatagramSocket? sock;
    try {
      // Bind to the discovery port so we can also receive broadcast beacons
      sock = await RawDatagramSocket.bind(InternetAddress.anyIPv4, _discPort, reuseAddress: true);
      sock.broadcastEnabled = true;
    } catch (_) {
      return null;
    }

    String? foundUrl;
    final completer = Completer<void>();
    late StreamSubscription sub;
    sub = sock.listen((event) {
      if (event == RawSocketEvent.read) {
        final dg = sock!.receive();
        if (dg == null) return;
        final msg = String.fromCharCodes(dg.data);
        if (msg.startsWith(_discRespPrefix)) {
          final url = msg.substring(_discRespPrefix.length).trim();
          if (url.startsWith('ws://')) {
            foundUrl = url;
            if (!completer.isCompleted) completer.complete();
          }
        }
      }
    });

    // Send broadcast queries: subnet-directed broadcast(s) and limited broadcast
    try {
      final subnets = await _collectLocalSubnets();
      for (final sn in subnets) {
        final bcast = _intToIpv4(sn.network | (~sn.mask & 0xFFFFFFFF));
        sock.send(_discQuery.codeUnits, InternetAddress(bcast), _discPort);
      }
      // Fallback: limited broadcast
      sock.send(_discQuery.codeUnits, InternetAddress('255.255.255.255'), _discPort);
    } catch (_) {}

    // Wait for first response or timeout
    try {
      await completer.future.timeout(Duration(milliseconds: timeoutMs));
    } catch (_) {}

    await sub.cancel();
    sock.close();
    return foundUrl;
  }

  Future<List<_Subnet>> _collectLocalSubnets() async {
    final out = <_Subnet>[];
    // Enumerate interfaces; assume sensible defaults if mask unknown
    try {
      final ifs = await NetworkInterface.list(includeLoopback: false, type: InternetAddressType.IPv4);
      for (final iface in ifs) {
        for (final addr in iface.addresses) {
          final ip = addr.address;
          final parts = ip.split('.');
          if (parts.length != 4) continue;
          final a = int.tryParse(parts[0]) ?? 0;
          final b = int.tryParse(parts[1]) ?? 0;
          int prefix;
          // Heuristic: private ranges typical defaults
          if (a == 10) {
            // Many networks use /16; scanning /8 would be too large
            prefix = 16;
          } else if (a == 172 && b >= 16 && b <= 31) {
            prefix = 16; // pragmatic default
          } else if (a == 192 && (int.tryParse(parts[1]) ?? 0) == 168) {
            prefix = 24;
          } else {
            prefix = 24;
          }
          final ipI = _ipv4ToInt(ip);
          final mask = prefix == 0 ? 0 : (~((1 << (32 - prefix)) - 1)) & 0xFFFFFFFF;
          final network = ipI & mask;
          final sn = _Subnet(network, mask, prefix, ip);
          // Avoid duplicates
          if (!out.any((e) => e.network == sn.network && e.prefix == sn.prefix)) {
            out.add(sn);
          }
        }
      }
    } catch (_) {}

    return out;
  }

  // Subnet host scanning removed (UDP discovery only)
  // === TCP discovery (unicast) ===
  // Scan reachable hosts in the local subnet(s) by attempting a short TCP connect
  // to port 81. If a host is reachable, try a WebSocket connect to confirm.
  Future<String?> _tcpDiscoverUrl({int timeoutMs = 800}) async {
    final subnets = await _collectLocalSubnets();
    if (subnets.isEmpty) return null;

    // Build a prioritized candidate list: for each subnet, probe a small set
    // of likely addresses first (gateway-ish, dhcp-ish, neighbors), then expand.
    final candidates = <InternetAddress>[];
    final seen = <String>{};
    for (final sn in subnets) {
      final first = sn.firstHost;
      final last = sn.lastHost;
      final self = _ipv4ToInt(sn.selfIp);

      // Helper to add if within range
      void addIf(int ipInt) {
        if (ipInt >= first && ipInt <= last && ipInt != self) {
          final addrStr = _intToIpv4(ipInt);
          if (seen.add(addrStr)) {
            candidates.add(InternetAddress(addrStr));
          }
        }
      }

      // Common candidates within a /24 (or narrower window if larger net)
      final base = (self & 0xFFFFFF00);
      final commonLastOctets = <int>[1, 10, 20, 50, 80, 100, 120, 150, 180, 200, 220, 240, 250];
      for (final lo in commonLastOctets) {
        addIf(base | lo);
      }

      // Neighbors around self
      for (int d = 1; d <= 6; d++) {
        addIf(self - d);
        addIf(self + d);
      }

      // Danach: kompletter Sweep des /24-Fensters (ganze Reichweite scannen)
      final start = (first > (base | 1)) ? first : (base | 1);
      final end = (last < (base | 255)) ? last : (base | 255);
      for (int ip = start; ip <= end; ip += 1) {
        if (ip == self) continue;
        addIf(ip);
      }
    }

    if (candidates.isEmpty) return null;

  // Probe with bounded concurrency
    const int maxConcurrent = 48;
    int idx = 0;
    String? found;
    final inFlight = <Future>[];

    Future<void> spawnNext() async {
      if (found != null) return; // early stop
      if (idx >= candidates.length) return;
      final addr = candidates[idx++];
      final f = _probeHost(addr, timeoutMs: timeoutMs).then((ok) async {
        if (ok && found == null) {
          // Perform a lightweight WS handshake to verify it's really our server so the ESP prints the connect event.
          final url = 'ws://${addr.address}:81/';
          try {
            final sock = await WebSocket.connect(url).timeout(Duration(milliseconds: timeoutMs));
            await sock.close();
            found = url;
            debugPrint('[DISCOVERY] WS handshake confirmed at ${addr.address}');
          } catch (e) {
            debugPrint('[DISCOVERY] Port 81 open at ${addr.address} but WS handshake failed: $e');
          }
        }
        if (found == null) {
          // continue pumping
          await spawnNext();
        }
      });
      inFlight.add(f);
    }

    // Start initial wave
    final initial = candidates.length < maxConcurrent ? candidates.length : maxConcurrent;
    for (int i = 0; i < initial; i++) {
      await spawnNext();
    }

    // Wait for completion or first found
    await Future.wait(inFlight);
    return found;
  }

  Future<bool> _probeHost(InternetAddress addr, {int timeoutMs = 400}) async {
    try {
      final s = await Socket.connect(addr, 81, timeout: Duration(milliseconds: timeoutMs));
      s.destroy();
      return true;
    } catch (_) {
      return false;
    }
  }

  // === Deadzone ===
  double _applyDeadzone(double v, [double deadzone = dz]) {
    final a = v.abs();
    if (a < deadzone) return 0;
    final s = (a - deadzone) / (1 - deadzone);
    return v.isNegative ? -s : s;
  }

  // Gamepad-Verbindung Status
  bool _gamepadConnected = false;

  // === Gamepad: Android-Stream lesen ===
  void _subscribeGamepad() {
    _gpSub = _gamepadChannel.receiveBroadcastStream().listen((event) {
      // event: { "lx": double, "r2": double, "l2": double, "connected": bool, "id": "..." }
      if (event is Map) {
        final connected = (event['connected'] as bool?) ?? false;
        _gamepadConnected = connected;
        final id = (event['id'] as String?) ?? '';
        _gpLabel = connected ? 'Gamepad: verbunden ($id)' : 'Gamepad: nicht verbunden';

        if (connected) {
          // NUR linken Stick für Lenkung verwenden, rechten Stick ignorieren
          // lx: -1..+1 / r2,l2: 0..1
          final lx = (event['lx'] as num?)?.toDouble() ?? 0.0;
          
          // Raw trigger values
          var r2Raw = ((event['r2'] as num?)?.toDouble() ?? 0.0).clamp(0.0, 1.0);
          var l2Raw = ((event['l2'] as num?)?.toDouble() ?? 0.0).clamp(0.0, 1.0);
          
          // === Automatische Kalibrierung ===
          // Update die maximalen beobachteten Werte für die Kalibrierung
          if (r2Raw > _maxObservedR2 && r2Raw > 0.05) {
            _maxObservedR2 = r2Raw;
          }
          
          if (l2Raw > _maxObservedL2 && l2Raw > 0.05) {
            _maxObservedL2 = l2Raw;
          }
          
          // DEBUG: Prüfen ob rechter Stick aktiv ist - NUR FÜR DEBUG!
          final isRightStickActive = (event['isRightStickActive'] as bool?) ?? false;
          final ry = (event['ry'] as num?)?.toDouble() ?? 0.0;
          if (isRightStickActive) {
            debugPrint('⚠️ WARNUNG: Rechter Stick ist aktiv! ry=$ry');
          }
          
          // *** ABSOLUT STRIKTE KONTROLLE DER EINGABEN ***
          
          // 1. NUR die Trigger L2/R2 für Gas/Bremse verwenden
          final r2 = r2Raw;
          final l2 = l2Raw;

          // 2. Steuern (links/rechts) - NUR linker Stick
          final steerAxis = _applyDeadzone(lx);
          
          // 3. KRITISCH: Gas/Bremse AUSSCHLIESSLICH über L2/R2 Trigger
          //    Absolut keine anderen Eingabequellen berücksichtigen
          final thrAxis = (r2 - l2).clamp(-1.0, 1.0);
          
          // 4. WICHTIG: Das rechte Stick-Flag für Debug erkennen, aber NICHT verwenden
          if (isRightStickActive && ry > 0.1) {
            debugPrint('⚠️⚠️⚠️ WARNUNG: Rechter Stick ist aktiv ($ry), wird aber ignoriert');
          }
          
          // Alle Debug-Werte in einer einzigen Zeile ausgeben
          debugPrint('📊 CONTROLLER: id=$id lx=$lx r2=$r2 l2=$l2 thr=$thrAxis finalValue=${thrAxis*maxVal}');

          _steer = steerAxis * maxVal;
          _throttle = thrAxis * maxVal;
        } else {
          // Gamepad getrennt - Werte zurücksetzen
          _steer = 0;
          _throttle = 0;
        }

        if (mounted) setState(() {});
      }
    }, onError: (_) {
      _gamepadConnected = false;
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
            // Hinweis: URL/Dev-Panel bleibt ganz oben; Gamepad-Panel wird im Control-Stack positioniert
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
                                  // run a one-shot search; if found, set URL and immediately connect
                                  final found = await _searchWSOnce();
                                  if (!mounted) return;
                                  if (found != null) {
                                    _wsUrlController.text = found;
                                    messenger.showSnackBar(SnackBar(content: Text('Found and connecting: $found')));
                                    // reconnect to the discovered URL
                                    _reconnectTimer?.cancel();
                                    await _ws?.close();
                                    _ws = null;
                                    setState(() {});
                                    _connectWS();
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
                  // Left joystick (throttle) - nur sichtbar wenn kein Gamepad verbunden
                  if (!_gamepadConnected)
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
                          // Nur verwenden wenn kein Gamepad verbunden
                          if (!_gamepadConnected) {
                            setState(() {
                              _throttle = (o.dy).clamp(-1.0, 1.0) * maxVal;
                            });
                          }
                        },
                        onEnd: () {
                          if (!_gamepadConnected) _throttle = 0;
                        },
                      ),
                    ),
                  // Right joystick (steer) - nur sichtbar wenn kein Gamepad verbunden
                  if (!_gamepadConnected)
                    Align(
                      alignment: Alignment(0.98, 0.6), // further out to the right
                      child: _EdgeStickyJoystick(
                        stickSize: stickSize,
                        knobSize: knobSize,
                        verticalOnly: false,
                        sensitivity: _sensitivity,
                        externalValue: Offset(_steerFilt / maxVal, 0),
                        onChanged: (o) {
                          // Nur verwenden wenn kein Gamepad verbunden
                          if (!_gamepadConnected) {
                            setState(() {
                              _steer = (o.dx).clamp(-1.0, 1.0) * maxVal;
                            });
                          }
                        },
                        onEnd: () {
                          if (!_gamepadConnected) _steer = 0;
                        },
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
                  // Gamepad-Status-Anzeige: oben rechts im Control-Bereich, damit Dev-Infos sichtbar bleiben
                  if (_gamepadConnected)
                    Align(
                      alignment: const Alignment(0.92, -0.92),
                      child: Container(
                        constraints: const BoxConstraints(maxWidth: 280),
                        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                        decoration: BoxDecoration(
                          color: const Color(0x90000000),
                          borderRadius: BorderRadius.circular(12),
                          border: Border.all(color: const Color(0xFF00FF00), width: 2),
                        ),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          crossAxisAlignment: CrossAxisAlignment.center,
                          children: [
                            Text(
                              '🎮 GAMEPAD AKTIV',
                              style: const TextStyle(
                                color: Color(0xFF00FF00),
                                fontSize: 14,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                            const SizedBox(height: 2),
                            const Text(
                              'Linker Stick: Lenkung\nL2/R2 Trigger: Gas/Bremse',
                              textAlign: TextAlign.center,
                              style: TextStyle(
                                color: Color(0xFF99AADD),
                                fontSize: 11,
                              ),
                            ),
                            const SizedBox(height: 2),
                            Builder(
                              builder: (context) {
                                final r2Value = (_throttle > 0) ? (_throttle / maxVal * 100).toInt() : 0;
                                final l2Value = (_throttle < 0) ? (-_throttle / maxVal * 100).toInt() : 0;
                                final gasColor = _throttle > 50
                                    ? const Color(0xFF00FF00)
                                    : _throttle < -50
                                        ? const Color(0xFFFF4444)
                                        : const Color(0xFFBBCCDD);
                                return Column(
                                  mainAxisSize: MainAxisSize.min,
                                  children: [
                                    Text(
                                      'L2: $l2Value% | R2: $r2Value%',
                                      textAlign: TextAlign.center,
                                      style: const TextStyle(
                                        color: Color(0xFFBBCCDD),
                                        fontSize: 10,
                                      ),
                                    ),
                                    const SizedBox(height: 2),
                                    Text(
                                      'Gas: ${_throttle.toInt()}',
                                      textAlign: TextAlign.center,
                                      style: TextStyle(
                                        color: gasColor,
                                        fontSize: 11,
                                        fontWeight: FontWeight.bold,
                                      ),
                                    ),
                                  ],
                                );
                              },
                            ),
                          ],
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
