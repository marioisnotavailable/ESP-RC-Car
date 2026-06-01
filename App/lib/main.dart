import 'package:esp_rc_car/connection_service.dart';
import 'package:esp_rc_car/controller_service.dart';
import 'package:esp_rc_car/ui/dev_panel.dart';
import 'package:esp_rc_car/ui/gamepad_status.dart';
import 'package:esp_rc_car/widgets/joystick.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]).then((_) {
    runApp(
      MultiProvider(
        providers: [
          ChangeNotifierProvider(create: (_) => ConnectionService()),
          ChangeNotifierProvider(
            create: (context) =>
                ControllerService(context.read<ConnectionService>()),
          ),
        ],
        child: const RCCarApp(),
      ),
    );
  });
}

class RCCarApp extends StatelessWidget {
  const RCCarApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ESP-RC Car',
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
  bool _showDevPanel = false;
  final GlobalKey _devPanelKey = GlobalKey();
  double _devPanelHeight = 0;

  void _toggleDevPanel() {
    setState(() => _showDevPanel = !_showDevPanel);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      final height = _devPanelKey.currentContext?.size?.height ?? 0;
      if ((height - _devPanelHeight).abs() > 0.5) {
        setState(() => _devPanelHeight = height);
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final screenWidth = MediaQuery.of(context).size.width;
    final isSmallScreen = screenWidth <= 700;
    // Joysticks: bigger in the normal driving view (full height is free). When the
    // dev panel is open the height is limited, so keep the size there but move them
    // up so they fill the empty space beside the WS indicator instead of sitting low.
    final stickSize = _showDevPanel
        ? (isSmallScreen ? 240.0 : 320.0)
        : (isSmallScreen ? 290.0 : 360.0);
    final knobSize = _showDevPanel
        ? (isSmallScreen ? 100.0 : 120.0)
        : (isSmallScreen ? 120.0 : 150.0);
    final stickAlignY = _showDevPanel ? 0.2 : 0.55;

    final controller = Provider.of<ControllerService>(context);

    final topOffset = _showDevPanel ? (_devPanelHeight + 16) : 0.0;

    return Scaffold(
      resizeToAvoidBottomInset: false,
      body: SafeArea(
        child: GestureDetector(
          onTap: () => FocusScope.of(context).unfocus(),
          child: Stack(
            children: [
              Positioned.fill(
                child: Padding(
                  padding: EdgeInsets.only(top: topOffset),
                  child: Column(
                    children: [
                      if (_showDevPanel) const SizedBox(height: 16),
                      Expanded(
                        child: Stack(
                          children: [
                            if (!controller.gamepadConnected) ...[
                              Align(
                                alignment: Alignment(-0.98, stickAlignY),
                                child: EdgeStickyJoystick(
                                  stickSize: stickSize,
                                  knobSize: knobSize,
                                  verticalOnly: true,
                                  externalValue: Offset(
                                    0,
                                    controller.throttle /
                                        ControllerService.maxVal,
                                  ),
                                  sensitivity: 0.8,
                                  onChanged:
                                      (offset) => controller.setThrottle(offset.dy),
                                  onEnd: () => controller.setThrottle(0),
                                ),
                              ),
                              Align(
                                alignment: Alignment(0.98, stickAlignY),
                                child: EdgeStickyJoystick(
                                  stickSize: stickSize,
                                  knobSize: knobSize,
                                  horizontalOnly: true,
                                  externalValue: Offset(
                                    controller.steer /
                                        ControllerService.maxVal,
                                    0,
                                  ),
                                  sensitivity: 0.8,
                                  onChanged:
                                      (offset) => controller.setSteer(offset.dx),
                                  onEnd: () => controller.setSteer(0),
                                ),
                              ),
                            ],
                            if (_showDevPanel)
                              Align(
                                alignment: const Alignment(0.0, -0.6),
                                child: Text(
                                  'Thr: ${controller.throttle.round()} | Steer: ${controller.steer.round()}',
                                  style: const TextStyle(
                                    color: Colors.white54,
                                    fontSize: 14,
                                  ),
                                ),
                              ),
                            GamepadStatus(
                              isConnected: controller.gamepadConnected,
                              throttle: controller.throttle,
                              maxVal: ControllerService.maxVal,
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              Align(
                alignment: Alignment.topCenter,
                child: DevPanel(
                  key: _devPanelKey,
                  isExpanded: _showDevPanel,
                  onToggle: _toggleDevPanel,
                ),
              ),
              Positioned(
                top: _showDevPanel ? (_devPanelHeight - 6) : 8,
                right: 12,
                child: const ConnectionStatusView(
                  showWsStatus: false,
                  showBattery: true,
                  rowAlignment: MainAxisAlignment.end,
                ),
              ),
              if (_showDevPanel)
                Positioned(
                  top: _devPanelHeight - 6,
                  left: 0,
                  right: 0,
                  child: const ConnectionStatusView(
                    showWsStatus: true,
                    showBattery: false,
                    rowAlignment: MainAxisAlignment.center,
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
