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
          ChangeNotifierProxyProvider<ConnectionService, ControllerService>(
            create: (context) => ControllerService(
              Provider.of<ConnectionService>(context, listen: false),
            ),
            update: (context, connectionService, previous) =>
                ControllerService(connectionService),
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

  @override
  Widget build(BuildContext context) {
    final screenWidth = MediaQuery.of(context).size.width;
    final isSmallScreen = screenWidth <= 700;
    final stickSize = isSmallScreen ? 240.0 : 320.0;
    final knobSize = isSmallScreen ? 100.0 : 120.0;

    final controller = Provider.of<ControllerService>(context);

    return Scaffold(
      resizeToAvoidBottomInset: false,
      body: SafeArea(
        child: GestureDetector(
          onTap: () => FocusScope.of(context).unfocus(),
          child: Column(
            children: [
              DevPanel(
                isExpanded: _showDevPanel,
                onToggle: () => setState(() => _showDevPanel = !_showDevPanel),
              ),
              const SizedBox(height: 16),
              Expanded(
                child: Stack(
                  children: [
                    if (_showDevPanel) const ConnectionStatusView(),
                    if (!controller.gamepadConnected) ...[
                      Align(
                        alignment: const Alignment(-0.98, 0.6),
                        child: EdgeStickyJoystick(
                          stickSize: stickSize,
                          knobSize: knobSize,
                          verticalOnly: true,
                          externalValue:
                              Offset(0, controller.throttle / ControllerService.maxVal),
                          sensitivity: 0.6,
                          onChanged: (offset) =>
                              controller.setThrottle(offset.dy),
                          onEnd: () => controller.setThrottle(0),
                        ),
                      ),
                      Align(
                        alignment: const Alignment(0.98, 0.6),
                        child: EdgeStickyJoystick(
                          stickSize: stickSize,
                          knobSize: knobSize,
                          horizontalOnly: true,
                          externalValue:
                              Offset(controller.steer / ControllerService.maxVal, 0),
                          sensitivity: 0.6,
                          onChanged: (offset) => controller.setSteer(offset.dx),
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
                              color: Colors.white54, fontSize: 14),
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
    );
  }
}
