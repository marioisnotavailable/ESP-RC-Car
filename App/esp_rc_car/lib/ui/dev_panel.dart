import 'package:esp_rc_car/connection_service.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

class DevPanel extends StatefulWidget {
  final bool isExpanded;
  final VoidCallback onToggle;

  const DevPanel({
    super.key,
    required this.isExpanded,
    required this.onToggle,
  });

  @override
  State<DevPanel> createState() => _DevPanelState();
}

class _DevPanelState extends State<DevPanel> {
  late final TextEditingController _wsUrlController;
  late final FocusNode _focusNode;
  late ConnectionService _connectionService;

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
                icon: Icon(widget.isExpanded ? Icons.expand_less : Icons.expand_more, color: Colors.white),
                onPressed: widget.onToggle,
                tooltip: 'Toggle Dev Panel',
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
                            contentPadding: EdgeInsets.symmetric(horizontal: 8, vertical: 8),
                            border: OutlineInputBorder(),
                          ),
                          onSubmitted: (url) => connectionService.connect(url, isManual: true),
                        ),
                      ),
                      const SizedBox(width: 8),
                      ElevatedButton(
                        onPressed: () => connectionService.connect(_wsUrlController.text, isManual: true),
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
        ],
      ),
    );
  }
}

class ConnectionStatusView extends StatelessWidget {
  const ConnectionStatusView({super.key});

  @override
  Widget build(BuildContext context) {
    final connectionService = context.watch<ConnectionService>();
    final status = connectionService.status;

    String statusText;
    Color statusColor;
    switch (status) {
      case ConnectionStatus.disconnected:
        statusText = 'Disconnected';
        statusColor = Colors.red;
        break;
      case ConnectionStatus.scanning:
        statusText = 'Scanning...';
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
      switch (method) {
        case DiscoveryMethod.udp:
          methodText = ' (UDP)';
          break;
        case DiscoveryMethod.tcp:
          methodText = ' (TCP)';
          break;
        case DiscoveryMethod.manual:
          methodText = ' (Manual)';
          break;
        case DiscoveryMethod.lastKnown:
          methodText = ' (Last Known)';
          break;
        case DiscoveryMethod.none:
          break;
      }
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 4.0),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(Icons.circle, color: statusColor, size: 12),
          const SizedBox(width: 8),
          Text(
            'WS: $statusText',
            style: TextStyle(color: statusColor, fontSize: 13),
          ),
          if (methodText.isNotEmpty)
            Text(
              methodText,
              style: TextStyle(color: Colors.grey[400], fontSize: 13),
            ),
        ],
      ),
    );
  }
}
