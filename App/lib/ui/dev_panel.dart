import 'dart:io';

import 'package:esp_rc_car/connection_service.dart';
import 'package:esp_rc_car/update_service.dart';
import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter/material.dart';
import 'package:open_filex/open_filex.dart';
import 'package:provider/provider.dart';

class DevPanel extends StatefulWidget {
  final bool isExpanded;
  final VoidCallback onToggle;

  const DevPanel({super.key, required this.isExpanded, required this.onToggle});

  @override
  State<DevPanel> createState() => _DevPanelState();
}

class _DevPanelState extends State<DevPanel> {
  late final TextEditingController _wsUrlController;
  late final FocusNode _focusNode;
  late ConnectionService _connectionService;
  bool _checkingUpdate = false;

  @override
  void initState() {
    super.initState();
    _connectionService = context.read<ConnectionService>();
    _wsUrlController = TextEditingController(text: _connectionService.wsUrl);
    _focusNode = FocusNode();
  }

  @override
  void dispose() {
    _wsUrlController.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  Future<void> _checkForUpdate() async {
    final messenger = ScaffoldMessenger.of(context);
    setState(() => _checkingUpdate = true);

    AppUpdateInfo? info;
    try {
      info = await UpdateService().checkForUpdate();
    } catch (e) {
      if (!mounted) return;
      setState(() => _checkingUpdate = false);
      messenger.showSnackBar(
        SnackBar(content: Text('Update-Prüfung fehlgeschlagen: $e')),
      );
      return;
    }

    if (!mounted) return;
    setState(() => _checkingUpdate = false);

    if (info == null) {
      messenger.showSnackBar(
        const SnackBar(content: Text('App ist aktuell ✔')),
      );
      return;
    }

    final update = info;
    final go = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Update verfügbar: v${update.version}'),
        content: SingleChildScrollView(
          child: Text(
            update.notes.isEmpty
                ? 'Eine neuere Version ist verfügbar.'
                : update.notes,
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Später'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Herunterladen & installieren'),
          ),
        ],
      ),
    );

    if (go == true) {
      await _downloadAndInstall(update);
    }
  }

  Future<void> _downloadAndInstall(AppUpdateInfo update) async {
    final messenger = ScaffoldMessenger.of(context);
    final progress = ValueNotifier<double>(0);

    showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        title: const Text('Lade Update herunter…'),
        content: ValueListenableBuilder<double>(
          valueListenable: progress,
          builder: (_, value, __) => Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              LinearProgressIndicator(value: value > 0 ? value : null),
              const SizedBox(height: 12),
              Text('${(value * 100).toStringAsFixed(0)} %'),
            ],
          ),
        ),
      ),
    );

    try {
      final path = await UpdateService().download(
        update.downloadUrl,
        onProgress: (p) => progress.value = p,
      );
      if (mounted) Navigator.of(context, rootNavigator: true).pop();
      final result = await OpenFilex.open(
        path,
        type: 'application/vnd.android.package-archive',
      );
      if (result.type != ResultType.done && mounted) {
        messenger.showSnackBar(
          SnackBar(
            content: Text('Installer konnte nicht geöffnet werden: ${result.message}'),
          ),
        );
      }
    } catch (e) {
      if (mounted) Navigator.of(context, rootNavigator: true).pop();
      if (mounted) {
        messenger.showSnackBar(
          SnackBar(content: Text('Download fehlgeschlagen: $e')),
        );
      }
    } finally {
      progress.dispose();
    }
  }

  @override
  Widget build(BuildContext context) {
    final connectionService = context.watch<ConnectionService>();

    // If the URL in the service changes (e.g., from auto-discovery),
    // update the text field, but only if the user isn't currently focused on it.
    if (!_focusNode.hasFocus) {
      final newUrl = connectionService.wsUrl;
      if (_wsUrlController.text != newUrl) {
        _wsUrlController.text = newUrl;
      }
    }

    return Padding(
      padding: const EdgeInsets.fromLTRB(8, 8, 8, 0),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              IconButton(
                icon: Icon(
                  widget.isExpanded ? Icons.expand_less : Icons.expand_more,
                  color: Colors.white,
                ),
                onPressed: widget.onToggle,
                tooltip: 'Toggle Dev Panel',
              ),
              if (widget.isExpanded && !kIsWeb && Platform.isAndroid)
                IconButton(
                  icon: _checkingUpdate
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.system_update, color: Colors.white70),
                  onPressed: _checkingUpdate ? null : _checkForUpdate,
                  tooltip: 'Auf App-Update prüfen',
                ),
              const SizedBox(width: 8),
              if (widget.isExpanded)
                Expanded(
                  child: Row(
                    children: [
                      Expanded(
                        child: TextField(
                          focusNode: _focusNode,
                          controller: _wsUrlController,
                          style: const TextStyle(color: Color(0xFF99AADD)),
                          decoration: const InputDecoration(
                            hintText: 'ws://...',
                            hintStyle: TextStyle(color: Color(0xFF6E7A8A)),
                            isDense: true,
                            contentPadding: EdgeInsets.symmetric(
                              horizontal: 8,
                              vertical: 8,
                            ),
                            border: OutlineInputBorder(),
                          ),
                          onSubmitted: (url) => connectionService.connect(
                            url,
                            isManual: true,
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      ElevatedButton(
                        onPressed: () => connectionService.connect(
                          _wsUrlController.text,
                          isManual: true,
                        ),
                        child: const Text('Connect'),
                      ),
                      const SizedBox(width: 8),
                      ElevatedButton(
                        onPressed: () {
                          final service = context.read<ConnectionService>();
                          if (service.status == ConnectionStatus.scanning) {
                            service.stopScan();
                          } else {
                            service.findAndConnect(withLastKnown: false);
                          }
                        },
                        child: Text(
                          connectionService.status == ConnectionStatus.scanning
                              ? 'Stop'
                              : 'Search',
                        ),
                      ),
                    ],
                  ),
                )
              else
                const Spacer(),
            ],
          ),
          if (widget.isExpanded) const SizedBox(height: 8),
        ],
      ),
    );
  }
}

class ConnectionStatusView extends StatelessWidget {
  final bool showWsStatus;
  final bool showBattery;
  final MainAxisAlignment rowAlignment;

  const ConnectionStatusView({
    super.key,
    this.showWsStatus = true,
    this.showBattery = true,
    this.rowAlignment = MainAxisAlignment.center,
  });

  @override
  Widget build(BuildContext context) {
    final connectionService = context.watch<ConnectionService>();
    final status = connectionService.status;
    final percent = connectionService.batteryPercent;

    String statusText;
    Color statusColor;
    switch (status) {
      case ConnectionStatus.disconnected:
        statusText = 'Disconnected';
        statusColor = Colors.red;
        break;
      case ConnectionStatus.scanning:
        final scanMethod = connectionService.discoveryMethod;
        statusText = 'Scanning${_scanMethodText(scanMethod)}...';
        statusColor = Colors.orange;
        break;
      case ConnectionStatus.connecting:
        statusText = 'Connecting...';
        statusColor = Colors.yellow;
        break;
      case ConnectionStatus.connected:
        statusText = 'Connected';
        statusColor = Colors.green;
        break;
    }

    String methodText = '';
    if (status == ConnectionStatus.connected) {
      final method = connectionService.discoveryMethod;
      methodText = _scanMethodText(method);
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 4.0),
      child: Row(
        mainAxisAlignment: rowAlignment,
        children: [
          if (showWsStatus) ...[
            Icon(Icons.circle, color: statusColor, size: 12),
            const SizedBox(width: 8),
            Text(
              'WS: $statusText',
              style: TextStyle(color: statusColor, fontSize: 13),
            ),
            if (methodText.isNotEmpty && status == ConnectionStatus.connected)
              Text(
                methodText,
                style: TextStyle(color: Colors.grey[400], fontSize: 13),
              ),
            const SizedBox(width: 12),
          ],
          if (showBattery) _buildBatteryIndicator(status, percent),
        ],
      ),
    );
  }

  Widget _buildBatteryIndicator(
    ConnectionStatus status,
    int? percent,
  ) {
    final color = _batteryColor(status, percent);
    final percentText = _batteryPercentText(status, percent);
    final icon = _batteryIcon(status, percent);

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          width: 48,
          height: 30,
          child: Stack(
            alignment: Alignment.center,
            children: [
              RotatedBox(
                quarterTurns: 1,
                child: Icon(icon, color: color, size: 40),
              ),
              Positioned.fill(
                child: Align(
                  // Material battery glyph has uneven vertical mass after rotation.
                  // A slight downward alignment centers the number visually in the body.
                  alignment: const Alignment(-0.10, 0.60),
                  child: Text(
                    percentText,
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: _batteryTextColor(status, percent),
                      fontSize: 11,
                      fontWeight: FontWeight.w700,
                      height: 1.0,
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  String _batteryPercentText(ConnectionStatus status, int? percent) {
    if (status != ConnectionStatus.connected || percent == null) {
      return '000';
    }
    return '${percent.clamp(0, 100)}';
  }

  IconData _batteryIcon(ConnectionStatus status, int? percent) {
    if (status != ConnectionStatus.connected || percent == null) {
      return Icons.battery_0_bar;
    }
    if (percent <= 10) return Icons.battery_alert;
    if (percent <= 25) return Icons.battery_2_bar;
    if (percent <= 50) return Icons.battery_3_bar;
    if (percent <= 75) return Icons.battery_5_bar;
    return Icons.battery_full;
  }

  Color _batteryColor(ConnectionStatus status, int? percent) {
    if (status != ConnectionStatus.connected) {
      return Colors.grey;
    }
    if (percent == null) {
      return Colors.orange;
    }
    if (percent <= 15) {
      return Colors.redAccent;
    }
    if (percent <= 35) {
      return Colors.orangeAccent;
    }
    return Colors.lightGreenAccent;
  }

  Color _batteryTextColor(ConnectionStatus status, int? percent) {
    if (status != ConnectionStatus.connected || percent == null) {
      return const Color(0xFFE0E0E0);
    }
    return Colors.black;
  }

  String _scanMethodText(DiscoveryMethod method) {
    switch (method) {
      case DiscoveryMethod.udp:
        return ' (UDP)';
      case DiscoveryMethod.tcp:
        return ' (TCP)';
      case DiscoveryMethod.manual:
        return ' (Manual)';
      case DiscoveryMethod.lastKnown:
        return ' (Last Known)';
      case DiscoveryMethod.none:
        return '';
    }
  }
}

