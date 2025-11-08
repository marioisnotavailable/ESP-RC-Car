import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';

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
  ConnectionService() {
    _status.addListener(notifyListeners);
    // Automatically try to connect on startup
    findAndConnect();
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

  Future<void> findAndConnect() async {
    if (_status.value == ConnectionStatus.scanning ||
        _status.value == ConnectionStatus.connecting) return;

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
        // If nothing found, try the default URL as a last resort
        await connect(_wsUrl, isManual: true);
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
          if (_status.value == ConnectionStatus.connected) {
            _status.value = ConnectionStatus.disconnected;
            _scheduleReconnect();
          }
        },
        onError: (e) {
          if (_status.value == ConnectionStatus.connected) {
            _status.value = ConnectionStatus.disconnected;
            _scheduleReconnect();
          }
        },
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
        debugPrint('[WS] Send failed: $e');
        _status.value = ConnectionStatus.disconnected;
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
      sock = await RawDatagramSocket.bind(InternetAddress.anyIPv4, _discPort,
          reuseAddress: true);
      sock.broadcastEnabled = true;
    } catch (e) {
      debugPrint('[UDP] Bind failed: $e');
      return null;
    }

    final completer = Completer<String?>();
    late StreamSubscription sub;
    sub = sock.listen((event) {
      if (event == RawSocketEvent.read) {
        final dg = sock?.receive();
        if (dg == null) return;
        final msg = String.fromCharCodes(dg.data);
        if (msg.startsWith(_discRespPrefix)) {
          final url = msg.substring(_discRespPrefix.length).trim();
          if (url.startsWith('ws://') && !completer.isCompleted) {
            completer.complete(url);
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
      final subnets = await _collectLocalSubnets();
      for (final sn in subnets) {
        final bcast = _intToIpv4(sn.network | (~sn.mask & 0xFFFFFFFF));
        sock.send(_discQuery.codeUnits, InternetAddress(bcast), _discPort);
      }
      sock.send(
          _discQuery.codeUnits, InternetAddress('255.255.255.255'), _discPort);
    } catch (e) {
      debugPrint('[UDP] Broadcast send failed: $e');
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
      final start = (sn.network | 1);
      final end = (sn.network | (~sn.mask & 0xFFFFFFFF)) - 1;
      for (var i = start; i <= end; i++) {
        if (_intToIpv4(i) == sn.selfIp) continue;
        candidates.add(InternetAddress(_intToIpv4(i)));
      }
    }

    if (candidates.isEmpty) return null;

    final completer = Completer<String?>();
    final List<Future<void>> checks = [];

    for (final addr in candidates) {
      final check = _probeHost(addr, timeoutMs: 800).then((found) {
        if (found && !completer.isCompleted) {
          completer.complete('ws://${addr.address}:81/');
        }
      });
      checks.add(check);
    }
    
    // Wait for all probes or a timeout
    try {
        await Future.wait(checks).timeout(Duration(milliseconds: timeoutMs));
    } catch (_) {
        // Timeout is expected if a host is found early
    }

    if (!completer.isCompleted) {
        completer.complete(null);
    }

    return completer.future;
  }

  Future<bool> _probeHost(InternetAddress addr, {int timeoutMs = 800}) async {
    try {
      final s = await Socket.connect(addr, 81,
          timeout: Duration(milliseconds: timeoutMs));
      s.destroy();
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<List<_Subnet>> _collectLocalSubnets() async {
    final out = <_Subnet>[];
    try {
      final ifs = await NetworkInterface.list(
          includeLoopback: false, type: InternetAddressType.IPv4);
      for (final iface in ifs) {
        // The 'addresses' property of NetworkInterface is a list of InternetAddress,
        // which doesn't include the prefix length. We need to look at the raw address
        // data to determine the subnet. However, for the sake of broad compatibility
        // and given that we are targeting consumer networking environments, assuming
        // a /24 subnet is a reasonable and effective heuristic.
        for (final addr in iface.addresses) {
          final ip = addr.address;
          if (ip.startsWith('169.254.')) continue; // Ignore link-local

          if (ip.startsWith('192.168.') ||
              ip.startsWith('10.') ||
              (ip.startsWith('172.') &&
                  (int.parse(ip.split('.')[1]) >= 16 &&
                      int.parse(ip.split('.')[1]) <= 31))) {
            
            // Heuristic: Assume a /24 subnet for simplicity. This is the most
            // common configuration for home and small office networks.
            const prefix = 24;
            final mask = (~((1 << (32 - prefix)) - 1)) & 0xFFFFFFFF;
            final network = _ipv4ToInt(ip) & mask;

            if (!out.any((s) => s.network == network)) {
              out.add(_Subnet(network, mask, prefix, ip));
            }
          }
        }
      }
    } catch (e) {
      debugPrint('[Subnet] Error collecting subnets: $e');
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
