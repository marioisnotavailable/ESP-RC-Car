import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum ConnectionStatus { disconnected, scanning, connecting, connected }

enum DiscoveryMethod { none, udp, tcp, manual, lastKnown }

class ConnectionService extends ChangeNotifier {
  static const _urlKey = 'ws_url';

  // --- Private State ---
  ConnectionStatus _status = ConnectionStatus.disconnected;
  DiscoveryMethod _discoveryMethod = DiscoveryMethod.none;
  WebSocket? _socket;
  String _wsUrl = 'ws://192.168.4.1:81/';
  Timer? _reconnectTimer;
  Completer<void>? _scanCompleter;

  // --- Constants ---
  static const int _discPort = 49352;
  static const String _discQuery = 'ESP_RC_DISCOVER';
  static const String _discRespPrefix = 'ESP_RC_HERE ';

  // --- Public Getters ---
  ConnectionStatus get status => _status;
  DiscoveryMethod get discoveryMethod => _discoveryMethod;
  WebSocket? get socket => _socket;
  String get wsUrl => _wsUrl;

  ConnectionService() {
    _loadUrl().then((_) {
      // Automatically try to connect on startup with the loaded URL
      findAndConnect();
    });
  }

  Future<void> _loadUrl() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      _wsUrl = prefs.getString(_urlKey) ?? _wsUrl;
      notifyListeners();
    } catch (e) {
      debugPrint('[Prefs] Failed to load URL: $e');
    }
  }

  Future<void> _saveUrl(String url) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_urlKey, url);
    } catch (e) {
      debugPrint('[Prefs] Failed to save URL: $e');
    }
  }

  /// Stops any ongoing scan.
  void stopScan() {
    if (_status == ConnectionStatus.scanning) {
      _scanCompleter?.complete(); // Signal the scan to stop
      _updateStatus(ConnectionStatus.disconnected);
    }
  }

  /// Finds the ESP-RC device and connects to it.
  ///
  /// The process is as follows:
  /// 1. Try connecting to the last known URL (if available).
  /// 2. If that fails, perform UDP broadcast discovery.
  /// 3. If that fails, perform a TCP subnet scan.
  /// 4. Connect to the first URL found.
  Future<void> findAndConnect({bool withLastKnown = true}) async {
    if (_status == ConnectionStatus.connecting ||
        _status == ConnectionStatus.scanning) {
      return;
    }

    _updateStatus(ConnectionStatus.scanning, DiscoveryMethod.none);
    _disconnect(quiet: true);
    _scanCompleter = Completer<void>();

    try {
      String? foundUrl;

      // 1. Try last known URL
      if (withLastKnown) {
        debugPrint('[Discovery] Trying last known URL: $_wsUrl');
        final connected = await _tryConnect(
          _wsUrl,
          timeout: const Duration(seconds: 2),
        );
        if (connected) {
          _updateStatus(ConnectionStatus.connected, DiscoveryMethod.lastKnown);
          return; // Success
        }
      }

      if (_scanCompleter?.isCompleted ?? false) return;

      // 2. UDP Discovery
      debugPrint('[Discovery] Starting UDP discovery...');
      foundUrl = await _udpDiscoverUrl(timeoutMs: 2000);
      if (foundUrl != null) {
        debugPrint('[Discovery] UDP found: $foundUrl');
        if (await _tryConnect(foundUrl)) {
          _updateStatus(ConnectionStatus.connected, DiscoveryMethod.udp);
          return; // Success
        }
      }

      if (_scanCompleter?.isCompleted ?? false) return;

      // 3. TCP Subnet Scan
      debugPrint('[Discovery] Starting TCP subnet scan...');
      foundUrl = await _tcpDiscoverUrl();
      if (foundUrl != null) {
        debugPrint('[Discovery] TCP scan found: $foundUrl');
        if (await _tryConnect(foundUrl)) {
          _updateStatus(ConnectionStatus.connected, DiscoveryMethod.tcp);
          return; // Success
        }
      }

      // 4. Failure
      debugPrint('[Discovery] Device not found.');
      _updateStatus(ConnectionStatus.disconnected);
    } catch (e) {
      debugPrint('[Discovery] Error: $e');
      if (_status != ConnectionStatus.connected) {
        _updateStatus(ConnectionStatus.disconnected);
      }
    } finally {
      if (!(_scanCompleter?.isCompleted ?? false)) {
        _scanCompleter?.complete();
      }
    }
  }

  /// Connects to a specific WebSocket URL.
  Future<void> connect(String url, {bool isManual = false}) async {
    if (_status == ConnectionStatus.connecting && url == _wsUrl) return;

    _updateStatus(
      ConnectionStatus.connecting,
      isManual ? DiscoveryMethod.manual : _discoveryMethod,
    );

    final connected = await _tryConnect(url);

    if (connected) {
      _updateStatus(
        ConnectionStatus.connected,
        isManual ? DiscoveryMethod.manual : _discoveryMethod,
      );
    } else {
      _updateStatus(ConnectionStatus.disconnected);
    }
  }

  // Internal connect helper, returns true on success
  Future<bool> _tryConnect(String url, {Duration? timeout}) async {
    _wsUrl = url;
    await _saveUrl(url);
    notifyListeners(); // Notify URL change

    try {
      _disconnect(quiet: true);
      _socket = await WebSocket.connect(
        url,
        compression: CompressionOptions.compressionOff,
      ).timeout(timeout ?? const Duration(seconds: 4));

      _updateStatus(ConnectionStatus.connected, _discoveryMethod);
      debugPrint('[WS] Connected to $url');

      _socket?.listen(
        (_) {}, // Ignore incoming data
        onDone: () {
          debugPrint('[WS] Disconnected');
          _disconnect();
          _scheduleReconnect();
        },
        onError: (error) {
          debugPrint('[WS] Error: $error');
          _disconnect();
          _scheduleReconnect();
        },
        cancelOnError: true,
      );
      return true;
    } catch (e) {
      debugPrint('[WS] Connection to $url failed: $e');
      if (_status != ConnectionStatus.connected) {
        _updateStatus(ConnectionStatus.disconnected);
      }
      return false;
    }
  }

  void send(String data) {
    if (_socket != null && _status == ConnectionStatus.connected) {
      try {
        _socket!.add(data);
      } catch (e) {
        debugPrint('[WS] Send error: $e');
        _disconnect();
        _scheduleReconnect();
      }
    }
  }

  void disconnect() {
    _disconnect();
  }

  void _disconnect({bool quiet = false}) {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _socket?.close();
    _socket = null;
    if (!quiet) {
      _updateStatus(ConnectionStatus.disconnected, DiscoveryMethod.none);
    }
  }

  void _scheduleReconnect() {
    if (_reconnectTimer?.isActive ?? false) return;
    // Only schedule if we were previously connected, to avoid looping on bad URLs
    _reconnectTimer = Timer(const Duration(seconds: 3), () {
      if (_status != ConnectionStatus.connected) {
        connect(_wsUrl);
      }
    });
  }

  void _updateStatus(ConnectionStatus status, [DiscoveryMethod? method]) {
    bool changed = false;
    if (_status != status) {
      _status = status;
      changed = true;
    }
    if (method != null && _discoveryMethod != method) {
      _discoveryMethod = method;
      changed = true;
    }
    if (changed) {
      notifyListeners();
    }
  }

  Future<String?> _udpDiscoverUrl({int timeoutMs = 1200}) async {
    final subnets = await _collectLocalSubnets();
    if (subnets.isEmpty) return null;

    RawDatagramSocket? sock;
    try {
      sock = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
      sock.broadcastEnabled = true;
    } catch (e) {
      debugPrint('[UDP] Socket creation failed: $e');
      return null;
    }

    final completer = Completer<String?>();
    final sub = sock.listen((event) {
      if (event == RawSocketEvent.read) {
        final dg = sock?.receive();
        if (dg == null) return;
        final resp = String.fromCharCodes(dg.data);
        if (resp.startsWith(_discRespPrefix)) {
          final url = resp.substring(_discRespPrefix.length).trim();
          if (url.isNotEmpty && !completer.isCompleted) {
            completer.complete(url);
          }
        }
      }
    });

    // Stop listening when the scan is cancelled or timed out
    final timeout = Timer(Duration(milliseconds: timeoutMs), () {
      if (!completer.isCompleted) completer.complete(null);
    });

    _scanCompleter?.future.then((_) {
      if (!completer.isCompleted) completer.complete(null);
    });

    // Send broadcast packets
    final data = _discQuery.codeUnits;
    try {
      // Send to limited broadcast
      sock.send(data, InternetAddress('255.255.255.255'), _discPort);
      // Send to subnet-directed broadcast for each interface
      for (final sn in subnets) {
        final bcastIp = _intToIpv4(sn.network | ~sn.mask);
        sock.send(data, InternetAddress(bcastIp), _discPort);
      }
    } catch (e) {
      debugPrint('[UDP] Broadcast failed: $e');
    }

    final result = await completer.future;
    timeout.cancel();
    await sub.cancel();
    sock.close();
    return result;
  }

  Future<String?> _tcpDiscoverUrl() async {
    final subnets = await _collectLocalSubnets();
    if (subnets.isEmpty) return null;

    final completer = Completer<String?>();
    final checks = <Future<void>>[];

    for (final sn in subnets) {
      final start = (sn.network & sn.mask) + 1;
      final end = (sn.network | ~sn.mask) - 1;
      for (var i = start; i <= end; i++) {
        if (_scanCompleter?.isCompleted ?? false) break;
        final ip = _intToIpv4(i);
        if (ip == sn.selfIp) continue;

        final addr = InternetAddress(ip);
        final check = _probeHost(addr).then((ok) {
          if (ok && !completer.isCompleted) {
            completer.complete('ws://${addr.address}:81/');
          }
        });
        checks.add(check);
      }
    }

    // Stop if scan is cancelled
    _scanCompleter?.future.then((_) {
      if (!completer.isCompleted) completer.complete(null);
    });

    // Wait for all probes to finish or for one to succeed.
    Future.wait(checks).then((_) {
      if (!completer.isCompleted) completer.complete(null);
    });

    return completer.future;
  }

  Future<bool> _probeHost(InternetAddress addr, {int timeoutMs = 800}) async {
    if (_scanCompleter?.isCompleted ?? false) return false;
    try {
      final s = await Socket.connect(
        addr,
        81,
        timeout: Duration(milliseconds: timeoutMs),
      );
      await s.close();
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<List<_Subnet>> _collectLocalSubnets() async {
    final out = <_Subnet>[];
    try {
      final interfaces = await NetworkInterface.list(
        includeLoopback: false,
        type: InternetAddressType.IPv4,
      );
      for (final interface in interfaces) {
        for (final addr in interface.addresses) {
          // Very basic check for a private, non-loopback, non-link-local address
          if (addr.isLoopback || addr.isLinkLocal) continue;

          final ip = addr.address;
          // Heuristic: assume /24 mask if not otherwise available.
          // network_info_plus could be more specific but this is more general.
          final parts = ip.split('.').map(int.parse).toList();
          final ipInt =
              (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
          final maskInt = 0xFFFFFF00; // Assume /24
          final netInt = ipInt & maskInt;

          out.add(_Subnet(netInt, maskInt, 24, ip));
        }
      }
    } catch (e) {
      debugPrint('[Net] Could not get network info: $e');
    }
    return out;
  }

  @override
  void dispose() {
    _disconnect();
    super.dispose();
  }
}

class _Subnet {
  final int network;
  final int mask;
  final int prefix;
  final String selfIp;
  _Subnet(this.network, this.mask, this.prefix, this.selfIp);
}

String _intToIpv4(int x) {
  final a = (x >> 24) & 0xFF;
  final b = (x >> 16) & 0xFF;
  final c = (x >> 8) & 0xFF;
  final d = x & 0xFF;
  return '$a.$b.$c.$d';
}
