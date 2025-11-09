import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum ConnectionStatus {
  disconnected,
  scanning,
  connecting,
  connected,
}

enum DiscoveryMethod {
  none,
  udp,
  tcp,
  manual,
}

class ConnectionService extends ChangeNotifier {
  static const _urlKey = 'ws_url';

  ConnectionService() {
    _status.addListener(notifyListeners);
    _loadUrl().then((_) {
      // Automatically try to connect on startup with the loaded URL
      findAndConnect();
    });
  }

  final ValueNotifier<ConnectionStatus> _status =
      ValueNotifier(ConnectionStatus.disconnected);
  ValueListenable<ConnectionStatus> get status => _status;

  final ValueNotifier<DiscoveryMethod> _discoveryMethod =
      ValueNotifier(DiscoveryMethod.none);
  ValueListenable<DiscoveryMethod> get discoveryMethod => _discoveryMethod;

  WebSocket? _socket;
  WebSocket? get socket => _socket;

  String _wsUrl = 'ws://192.168.4.1:81/';
  String get wsUrl => _wsUrl;

  Timer? _reconnectTimer;

  static const int _discPort = 49352;
  static const String _discQuery = 'ESP_RC_DISCOVER';
  static const String _discRespPrefix = 'ESP_RC_HERE ';

  Future<void> _loadUrl() async {
    final prefs = await SharedPreferences.getInstance();
    _wsUrl = prefs.getString(_urlKey) ?? _wsUrl;
    notifyListeners();
  }

  Future<void> _saveUrl(String url) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_urlKey, url);
  }

  Future<void> findAndConnect() async {
    if (_status.value == ConnectionStatus.scanning ||
        _status.value == ConnectionStatus.connecting) {
      return;
    }

    _status.value = ConnectionStatus.scanning;
    _discoveryMethod.value = DiscoveryMethod.none;
    _disconnect(quiet: true);

    try {
      // Try UDP discovery first
      String? foundUrl = await _udpDiscoverUrl(timeoutMs: 1500);
      if (foundUrl != null) {
        _discoveryMethod.value = DiscoveryMethod.udp;
      }

      // Fallback to TCP scan if UDP fails
      foundUrl ??= await _tcpDiscoverUrl(timeoutMs: 4000);
      if (foundUrl != null && _discoveryMethod.value == DiscoveryMethod.none) {
        _discoveryMethod.value = DiscoveryMethod.tcp;
      }

      if (foundUrl != null) {
        await connect(foundUrl);
      } else {
        // If discovery fails, try connecting to the last known URL
        await connect(_wsUrl);
      }
    } catch (e) {
      debugPrint('[Discovery] Error: $e');
      _status.value = ConnectionStatus.disconnected;
    }
  }

  Future<void> connect(String url, {bool isManual = false}) async {
    if (_status.value == ConnectionStatus.connecting && url == _wsUrl) return;

    _status.value = ConnectionStatus.connecting;
    _wsUrl = url;
    if (isManual) {
      _discoveryMethod.value = DiscoveryMethod.manual;
    }
    await _saveUrl(url); // Save the URL
    notifyListeners();

    try {
      _disconnect(quiet: true);
      _socket = await WebSocket.connect(url).timeout(const Duration(seconds: 4));
      _status.value = ConnectionStatus.connected;
      debugPrint('[WS] Connected to $url');

      _socket?.listen(
        (data) {
          // Handle incoming data if needed
        },
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
    } catch (e) {
      debugPrint('[WS] Connection to $url failed: $e');
      _status.value = ConnectionStatus.disconnected;
      _scheduleReconnect();
    }
  }

  void send(String data) {
    if (_socket != null && _status.value == ConnectionStatus.connected) {
      try {
        _socket!.add(data);
      } catch (e) {
        debugPrint('[WS] Send error: $e');
        _disconnect();
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
      _status.value = ConnectionStatus.disconnected;
      _discoveryMethod.value = DiscoveryMethod.none;
    }
  }

  void _scheduleReconnect() {
    if (_reconnectTimer?.isActive ?? false) return;
    // Only schedule if we were previously connected, to avoid looping on bad URLs
    _reconnectTimer = Timer(const Duration(seconds: 3), () {
       if (_status.value != ConnectionStatus.connected) {
        connect(_wsUrl);
       }
    });
  }

  Future<String?> _udpDiscoverUrl({int timeoutMs = 1200}) async {
    RawDatagramSocket? sock;
    try {
      sock = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
      sock.broadcastEnabled = true;
    } catch (e) {
      debugPrint('[UDP] Socket creation failed: $e');
      return null;
    }

    final completer = Completer<String?>();
    late StreamSubscription sub;
    sub = sock.listen((event) {
      if (event == RawSocketEvent.read) {
        final dg = sock?.receive();
        if (dg == null) return;
        final resp = String.fromCharCodes(dg.data);
        if (resp.startsWith(_discRespPrefix)) {
          final url = resp.substring(_discRespPrefix.length);
          if (!completer.isCompleted) {
            completer.complete('ws://$url:81/');
          }
        }
      }
    });

    Timer(Duration(milliseconds: timeoutMs), () {
      if (!completer.isCompleted) {
        completer.complete(null);
      }
    });

    try {
      sock.send(_discQuery.codeUnits, InternetAddress('255.255.255.255'), _discPort);
    } catch (e) {
      debugPrint('[UDP] Broadcast failed: $e');
    }

    final result = await completer.future;
    await sub.cancel();
    sock.close();
    return result;
  }

  Future<String?> _tcpDiscoverUrl({int timeoutMs = 4000}) async {
    final subnets = await _collectLocalSubnets();
    if (subnets.isEmpty) return null;

    final candidates = <InternetAddress>{};
    for (final sn in subnets) {
      final start = (sn.network & sn.mask) + 1;
      final end = (sn.network | ~sn.mask) - 1;
      for (var i = start; i <= end; i++) {
        final ip = _intToIpv4(i);
        if (ip != sn.selfIp) candidates.add(InternetAddress(ip));
      }
    }

    if (candidates.isEmpty) return null;

    final completer = Completer<String?>();
    final List<Future<void>> checks = [];

    for (final addr in candidates) {
      final check = _probeHost(addr).then((ok) {
        if (ok && !completer.isCompleted) {
          completer.complete('ws://${addr.address}:81/');
        }
      });
      checks.add(check);
    }
    
    // Wait for all probes or a timeout
    try {
        await Future.wait(checks).timeout(Duration(milliseconds: timeoutMs));
    } catch (_) {
        // This is expected if we time out before all probes complete
    }

    if (!completer.isCompleted) {
      completer.complete(null);
    }

    return completer.future;
  }

  Future<bool> _probeHost(InternetAddress addr, {int timeoutMs = 800}) async {
    try {
      final s = await Socket.connect(addr, 81, timeout: Duration(milliseconds: timeoutMs));
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
      for (final i in interfaces) {
        for (final a in i.addresses) {
          // Poor man's subnet mask calculation. Assumes /24 for private IPs.
          // This is not robust but works for most home networks.
          if (a.address.startsWith('192.168.') ||
              a.address.startsWith('10.') ||
              a.address.startsWith('172.')) {
            final ipInt = _ipv4ToInt(a.address);
            const maskInt = 0xFFFFFF00; // /24
            final netInt = ipInt & maskInt;
            out.add(_Subnet(netInt, maskInt, 24, a.address));
          }
        }
      }
    } catch (e) {
      debugPrint('[Net] Could not list interfaces: $e');
    }
    return out;
  }

  @override
  void dispose() {
    _disconnect();
    _status.removeListener(notifyListeners);
    _status.dispose();
    _discoveryMethod.dispose();
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
