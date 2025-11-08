import 'package:esp_rc_car/connection_service.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

class DevPanel extends StatefulWidget {
  const DevPanel({super.key});

  @override
  State<DevPanel> createState() => _DevPanelState();
}

class _DevPanelState extends State<DevPanel> {
  bool _showDevPanel = false;

  @override
  Widget build(BuildContext context) {
    final connectionService = context.watch<ConnectionService>();
    final wsUrlController = TextEditingController(text: connectionService.wsUrl);

    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(8, 8, 8, 6),
          child: Row(
            children: [
              IconButton(
                icon: Icon(_showDevPanel ? Icons.expand_less : Icons.expand_more, color: Colors.white),
                onPressed: () => setState(() => _showDevPanel = !_showDevPanel),
                tooltip: 'Toggle Dev Panel',
              ),
              const SizedBox(width: 8),
              if (_showDevPanel)
                Expanded(
                  child: Row(
                    children: [
                      Expanded(
                        child: TextField(
                          controller: wsUrlController,
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
                        onPressed: () => connectionService.connect(wsUrlController.text, isManual: true),
                        child: const Text('Connect'),
                      ),
                      const SizedBox(width: 8),
                      ElevatedButton(
                        onPressed: () => connectionService.findAndConnect(),
                        child: const Text('Search'),
                      ),
                    ],
                  ),
                )
              else
                const Spacer(),
            ],
          ),
        ),
        if (_showDevPanel)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 4.0),
            child: ValueListenableBuilder<ConnectionStatus>(
              valueListenable: connectionService.status,
              builder: (context, status, child) {
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
                return Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(Icons.circle, color: statusColor, size: 12),
                    const SizedBox(width: 8),
                    Text(
                      'WS: $statusText',
                      style: TextStyle(color: statusColor, fontSize: 13),
                    ),
                    if (status == ConnectionStatus.connected)
                      ValueListenableBuilder<DiscoveryMethod>(
                        valueListenable: connectionService.discoveryMethod,
                        builder: (context, method, child) {
                          String methodText = '';
                          switch (method) {
                            case DiscoveryMethod.udp:
                              methodText = ' (UDP Beacon)';
                              break;
                            case DiscoveryMethod.tcp:
                              methodText = ' (TCP Scan)';
                              break;
                            case DiscoveryMethod.manual:
                              methodText = ' (Manual)';
                              break;
                            case DiscoveryMethod.none:
                              break;
                          }
                          return Text(
                            methodText,
                            style: TextStyle(color: Colors.grey[400], fontSize: 13),
                          );
                        },
                      ),
                  ],
                );
              },
            ),
          ),
      ],
    );
  }
}
