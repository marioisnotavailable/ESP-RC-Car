import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:network_info_plus/network_info_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum ConnectionStatus { disconnected, scanning, connecting, connected }

enum DiscoveryMethod { none, udp, tcp, manual, lastKnown }

class ConnectionService extends ChangeNotifier {
  static const _urlKey = 'ws_url';
  static const _battPrefix = 'BATT:';

  // --- Private State ---
  ConnectionStatus _status = ConnectionStatus.disconnected;
  DiscoveryMethod _discoveryMethod = DiscoveryMethod.none;
  WebSocket? _socket;
  String _wsUrl = 'ws://192.168.4.1:81/';
  String _lastSuccessfulUrl = 'ws://192.168.4.1:81/';
  Timer? _reconnectTimer;
  Completer<void>? _scanCompleter;
  int? _batteryPercent;

  // --- Constants ---
  static const int _discPort = 49352;
  static const String _discQuery = 'ESP_RC_DISCOVER';
  static const String _discRespPrefix = 'ESP_RC_HERE ';
  static const int _tcpProbeDelayMs = 5;

  // --- Public Getters ---
  ConnectionStatus get status => _status;
  DiscoveryMethod get discoveryMethod => _discoveryMethod;
  WebSocket? get socket => _socket;
  String get wsUrl => _wsUrl;
  int? get batteryPercent => _batteryPercent;

  ConnectionService() {
    _loadUrl().then((_) {
      // Automatically try to connect on startup with the loaded URL
      findAndConnect();
    });
  }

  Future<void> _loadUrl() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final stored = prefs.getString(_urlKey);
      if (stored != null && stored.isNotEmpty) {
        _lastSuccessfulUrl = stored;
        _wsUrl = stored;
      } else {
        _wsUrl = _lastSuccessfulUrl;
      }
      notifyListeners();
    } catch (e) {
      debugPrint('[Prefs] Failed to load URL: $e');
    }
  }

  Future<void> _saveUrl(String url) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_urlKey, url);
      _lastSuccessfulUrl = url;
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

    _disconnect(quiet: true);
    _scanCompleter = Completer<void>();
    _updateStatus(ConnectionStatus.scanning, DiscoveryMethod.none);

    try {
      if (withLastKnown && _lastSuccessfulUrl.isNotEmpty) {
        debugPrint('[Discovery] Trying last known URL: $_lastSuccessfulUrl');
        final connected = await _connectUsing(
          _lastSuccessfulUrl,
          DiscoveryMethod.lastKnown,
          timeout: const Duration(seconds: 2),
        );
        if (connected) {
          return;
        }
      }

      if (_scanCompleter?.isCompleted ?? false) return;

      _updateStatus(ConnectionStatus.scanning, DiscoveryMethod.udp);
      debugPrint('[Discovery] Starting UDP discovery...');
      final foundUdpUrl = await _udpDiscoverUrl(timeoutMs: 2000);
      if (foundUdpUrl != null && !(_scanCompleter?.isCompleted ?? false)) {
        debugPrint('[Discovery] UDP found: $foundUdpUrl');
        if (await _connectUsing(foundUdpUrl, DiscoveryMethod.udp)) {
          return;
        }
      }

      if (_scanCompleter?.isCompleted ?? false) return;

      _updateStatus(ConnectionStatus.scanning, DiscoveryMethod.tcp);
      debugPrint('[Discovery] Starting TCP subnet scan...');
      final foundTcpUrl = await _tcpDiscoverUrl();
      if (foundTcpUrl != null && !(_scanCompleter?.isCompleted ?? false)) {
        debugPrint('[Discovery] TCP scan found: $foundTcpUrl');
        if (await _connectUsing(foundTcpUrl, DiscoveryMethod.tcp)) {
          return;
        }
      }

      debugPrint('[Discovery] Device not found.');
      _updateStatus(ConnectionStatus.disconnected, DiscoveryMethod.none);
    } catch (e) {
      debugPrint('[Discovery] Error: $e');
      if (_status != ConnectionStatus.connected) {
        _updateStatus(ConnectionStatus.disconnected, DiscoveryMethod.none);
      }
    } finally {
      if (!(_scanCompleter?.isCompleted ?? false)) {
        _scanCompleter?.complete();
      }
      _scanCompleter = null;
    }
  }

  /// Connects to a specific WebSocket URL.
  Future<void> connect(
    String url, {
    bool isManual = false,
    DiscoveryMethod? reason,
  }) async {
    if (_status == ConnectionStatus.connecting && url == _wsUrl) return;

    final method = reason ?? (isManual ? DiscoveryMethod.manual : _discoveryMethod);
    await _connectUsing(url, method);
  }

  Future<bool> _connectUsing(
    String url,
    DiscoveryMethod method, {
    Duration? timeout,
  }) async {
    final target = url.trim();
    if (target.isEmpty) return false;

    _updateStatus(ConnectionStatus.connecting, method);
    final success = await _tryConnect(
      target,
      timeout: timeout,
      method: method,
    );

    if (!success) {
      if (_scanCompleter != null && !(_scanCompleter?.isCompleted ?? false)) {
        _updateStatus(ConnectionStatus.scanning, DiscoveryMethod.none);
      } else {
        _updateStatus(ConnectionStatus.disconnected, DiscoveryMethod.none);
      }
    }

    return success;
  }

  // Internal connect helper, returns true on success
  Future<bool> _tryConnect(
    String url, {
    Duration? timeout,
    DiscoveryMethod? method,
  }) async {
    final targetUrl = url.trim();
    if (targetUrl.isEmpty) return false;

    if (_wsUrl != targetUrl) {
      _wsUrl = targetUrl;
      notifyListeners();
    }

    try {
      _disconnect(quiet: true);
      final socket = await WebSocket.connect(
        targetUrl,
        compression: CompressionOptions.compressionOff,
      ).timeout(timeout ?? const Duration(seconds: 4));

      _socket = socket;
      _socket?.listen(
        _handleIncomingMessage,
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

      await _applyConnectedState(targetUrl, method ?? _discoveryMethod);
      debugPrint('[WS] Connected to $targetUrl');
      return true;
    } catch (e) {
      debugPrint('[WS] Connection to $targetUrl failed: $e');
      _disconnect(quiet: true);
      return false;
    }
  }

  Future<void> _applyConnectedState(
    String url,
    DiscoveryMethod method,
  ) async {
    _discoveryMethod = method;
    if (_lastSuccessfulUrl != url) {
      _lastSuccessfulUrl = url;
      await _saveUrl(url);
    }
    _updateStatus(ConnectionStatus.connected, method);
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

  void _handleIncomingMessage(dynamic data) {
    if (data == null) return;
    final msg = data.toString().trim();
    if (msg.isEmpty) return;

    if (msg.startsWith(_battPrefix)) {
      final rawValue = msg.substring(_battPrefix.length).trim();
      final percent = int.tryParse(rawValue);
      if (percent == null) {
        debugPrint('[WS] Invalid battery payload: $msg');
        return;
      }
      _setBatteryPercent(percent);
    }
  }

  void _setBatteryPercent(int value) {
    if (_batteryPercent == value) return;
    _batteryPercent = value;
    notifyListeners();
  }

  void _disconnect({bool quiet = false}) {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
    _socket?.close();
    _socket = null;
    if (_batteryPercent != null) {
      _batteryPercent = null;
    }
    if (!quiet) {
      _updateStatus(ConnectionStatus.disconnected, DiscoveryMethod.none);
    } else {
      notifyListeners();
    }
  }

  void _scheduleReconnect() {
    if (_reconnectTimer?.isActive ?? false) return;
    final retryUrl = _lastSuccessfulUrl.trim().isNotEmpty
        ? _lastSuccessfulUrl
        : _wsUrl;
    if (retryUrl.trim().isEmpty) return;

    _reconnectTimer = Timer(const Duration(seconds: 3), () {
      if (_status == ConnectionStatus.connected) return;
      connect(retryUrl, reason: DiscoveryMethod.lastKnown);
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
        final bcastIp = _intToIpv4(sn.network | (~sn.mask & 0xFFFFFFFF));
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
    if (subnets.isEmpty) {
      debugPrint('[Discovery] TCP scan failed: No local subnets found. (Is Wi-Fi on?)');
      return null;
    }

    debugPrint('[Discovery] TCP scan found ${subnets.length} subnet(s). Starting probes...');

    final startTime = DateTime.now();
    final completer = Completer<String?>();
    var pendingProbes = 0;
    var completedProbes = 0;

    // Stop if the user cancels the scan
    _scanCompleter?.future.then((_) {
      if (!completer.isCompleted) completer.complete(null);
    });

    for (final sn in subnets) {
      if (completer.isCompleted) break;
      if (sn.prefix <= 0 || sn.prefix >= 31) {
        debugPrint('[Discovery]   - Skipping subnet ${sn.selfIp} (prefix ${sn.prefix})');
        continue;
      }

      final networkBase = _normalize32(sn.network & sn.mask);
      final broadcast = _normalize32(sn.network | (~sn.mask & 0xFFFFFFFF));
      if (broadcast <= networkBase + 1) {
        debugPrint('[Discovery]   - Skipping subnet ${sn.selfIp} (no host addresses)');
        continue;
      }

      final start = networkBase + 1;
      final end = broadcast - 1;
      final hostCount = end >= start ? (end - start + 1) : 0;
      final maskIp = _intToIpv4(_normalize32(sn.mask));
      final networkIp = _intToIpv4(networkBase);
      final broadcastIp = _intToIpv4(broadcast);
      debugPrint(
        '[Discovery]   - Subnet $networkIp/${sn.prefix} mask $maskIp broadcast '
        '$broadcastIp (self ${sn.selfIp}), probing $hostCount host(s)...',
      );

      for (var i = start; i <= end; i++) {
        if (_scanCompleter?.isCompleted ?? false) break;
        if (completer.isCompleted) break;
        pendingProbes++;
        final ipInt = i;
        final scheduledIndex = pendingProbes - 1;
        final delay = Duration(milliseconds: scheduledIndex * _tcpProbeDelayMs);

        () async {
          try {
            if (delay > Duration.zero) {
              await Future.delayed(delay);
            }
            if (_scanCompleter?.isCompleted ?? false) return;
            if (completer.isCompleted) return;

            final ip = _intToIpv4(ipInt);
            if (ip == sn.selfIp) return;

            final addr = InternetAddress(ip);
            final ok = await _probeHost(addr);
            if (ok && !completer.isCompleted) {
              final elapsed = DateTime.now().difference(startTime);
              final attempts = completedProbes + 1;
              debugPrint(
                '[Discovery] TCP probe success at $ip after ${elapsed.inMilliseconds}ms '
                '($attempts host(s) tested).',
              );
              completer.complete('ws://${addr.address}:81/');
            }
          } finally {
            pendingProbes -= 1;
            if (pendingProbes < 0) {
              pendingProbes = 0;
            }
            completedProbes += 1;
            if (pendingProbes == 0 && !completer.isCompleted) {
              final elapsed = DateTime.now().difference(startTime);
              debugPrint(
                '[Discovery] All TCP probes finished after ${elapsed.inMilliseconds}ms '
                '($completedProbes host(s) tested), no device found.',
              );
              completer.complete(null);
            }
          }
        }();
      }
    }

    if (pendingProbes == 0 && !completer.isCompleted) {
      final elapsed = DateTime.now().difference(startTime);
      debugPrint(
        '[Discovery] All TCP probes finished after ${elapsed.inMilliseconds}ms '
        '($completedProbes host(s) tested), no device found.',
      );
      completer.complete(null);
    }

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
        includeLinkLocal: false,
        type: InternetAddressType.IPv4,
      );
      for (final interface in interfaces) {
        for (final addr in interface.addresses) {
          if (addr.isLoopback || addr.isLinkLocal) continue;

          final ipInt = _ipv4ToInt(addr.address);
          if (ipInt == null) continue;

          const defaultPrefix = 24;
          final maskInt = _prefixToMask(defaultPrefix);
          final netInt = ipInt & maskInt;
          out.add(_Subnet(netInt, maskInt, defaultPrefix, addr.address));
        }
      }
    } catch (e) {
      debugPrint('[Net] Could not get network info: $e');
    }

    await _overrideWithWifiInfo(out);
    return out;
  }

  Future<void> _overrideWithWifiInfo(List<_Subnet> subnets) async {
    try {
      final info = NetworkInfo();
      final wifiIp = await info.getWifiIP();
      if (wifiIp == null || wifiIp.isEmpty) return;

      final maskStr = await info.getWifiSubmask();
      final maskInt = _ipv4ToInt(maskStr);
      final ipInt = _ipv4ToInt(wifiIp);
      if (maskInt == null || ipInt == null) return;

      final prefix = _maskToPrefix(maskInt);
      final netInt = ipInt & maskInt;
      final updated = _Subnet(netInt, maskInt, prefix, wifiIp);

      final existingIndex = subnets.indexWhere((sn) => sn.selfIp == wifiIp);
      if (existingIndex >= 0) {
        subnets[existingIndex] = updated;
      } else {
        subnets.add(updated);
      }
    } catch (e) {
      debugPrint('[Net] network_info_plus override failed: $e');
    }
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
  _Subnet(int network, int mask, this.prefix, this.selfIp)
      : network = _normalize32(network),
        mask = _normalize32(mask);
}

String _intToIpv4(int x) {
  final normalized = _normalize32(x);
  final a = (normalized >> 24) & 0xFF;
  final b = (normalized >> 16) & 0xFF;
  final c = (normalized >> 8) & 0xFF;
  final d = normalized & 0xFF;
  return '$a.$b.$c.$d';
}

int? _ipv4ToInt(String? ip) {
  if (ip == null || ip.isEmpty) return null;
  final parts = ip.split('.');
  if (parts.length != 4) return null;
  int value = 0;
  for (final part in parts) {
    final octet = int.tryParse(part);
    if (octet == null || octet < 0 || octet > 255) return null;
    value = (value << 8) | octet;
  }
  return _normalize32(value);
}

int _maskToPrefix(int mask) {
  int count = 0;
  int value = _normalize32(mask);
  while (value != 0) {
    if ((value & 0x80000000) == 0) break;
    count++;
    value = (value << 1) & 0xFFFFFFFF;
  }
  if (count < 0) return 0;
  if (count > 32) return 32;
  return count;
}

int _normalize32(int value) => value & 0xFFFFFFFF;

int _prefixToMask(int prefix) {
  if (prefix <= 0) return 0;
  if (prefix >= 32) return 0xFFFFFFFF;
  final shift = 32 - prefix;
  return _normalize32(0xFFFFFFFF << shift);
}
