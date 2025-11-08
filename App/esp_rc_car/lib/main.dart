import 'dart:async';
import 'package:esp_rc_car/connection_service.dart';
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
      ChangeNotifierProvider(
        create: (_) => ConnectionService(),
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
  // Services
  late ConnectionService _connectionService;

  // Control Values
  static const int maxVal = 1000;
  double _throttle = 0;
  double _steer = 0;

  // Filtering
  static const double steerDeadzone = 0.08;
  static const double steerFilterAlpha = 0.30;
  static const double throttleFilterAlpha = 0.30;
  double _steerFilt = 0;
  double _thrFilt = 0;

  // Gamepad
  static const _gamepadChannel = EventChannel('rc.gamepad/events');
  StreamSubscription? _gpSub;
  bool _gamepadConnected = false;

  // Main Loop
  Timer? _loop;
  static const int sendHz = 50;

  @override
  void initState() {
    super.initState();
    _connectionService = Provider.of<ConnectionService>(context, listen: false);
    _subscribeToGamepad();
    _startControlLoop();
  }

  @override
  void dispose() {
    _loop?.cancel();
    _gpSub?.cancel();
    super.dispose();
  }

  void _sendControls() {
    final msg = '${_thrFilt.round()},${_steerFilt.round()},0';
    _connectionService.send(msg);
  }

  double _applyDeadzone(double value, [double deadzone = steerDeadzone]) {
    final absValue = value.abs();
    if (absValue < deadzone) return 0;
    final sign = value.sign;
    return sign * (absValue - deadzone) / (1 - deadzone);
  }

  void _subscribeToGamepad() {
    _gpSub = _gamepadChannel.receiveBroadcastStream().listen((event) {
      if (event is! Map) return;

      final bool connected = event['connected'] ?? false;
      if (_gamepadConnected != connected) {
        setState(() => _gamepadConnected = connected);
      }

      if (connected) {
        final lx = (event['lx'] as num?)?.toDouble() ?? 0.0;
        final r2 = (event['r2'] as num?)?.toDouble() ?? 0.0;
        final l2 = (event['l2'] as num?)?.toDouble() ?? 0.0;

        final steerAxis = _applyDeadzone(lx);
        final thrAxis = (r2 - l2).clamp(-1.0, 1.0);

        _steer = steerAxis * maxVal;
        _throttle = thrAxis * maxVal;
      } else {
        _steer = 0;
        _throttle = 0;
      }
    }, onError: (_) {
      if (_gamepadConnected) {
        setState(() => _gamepadConnected = false);
      }
    });
  }

  void _startControlLoop() {
    _loop = Timer.periodic(Duration(milliseconds: (1000 / sendHz).round()), (_) {
      // Apply smoothing filters
      _steerFilt = steerFilterAlpha * _steerFilt + (1 - steerFilterAlpha) * _steer;
      _thrFilt = throttleFilterAlpha * _thrFilt + (1 - throttleFilterAlpha) * _throttle;

      // Clamp values to ensure they are within the expected range
      _steerFilt = _steerFilt.clamp(-maxVal.toDouble(), maxVal.toDouble());
      _thrFilt = _thrFilt.clamp(-maxVal.toDouble(), maxVal.toDouble());

      _sendControls();

      if (mounted) setState(() {});
    });
  }

  @override
  Widget build(BuildContext context) {
    final screenWidth = MediaQuery.of(context).size.width;
    final isSmallScreen = screenWidth <= 700;
    final stickSize = isSmallScreen ? 240.0 : 320.0;
    final knobSize = isSmallScreen ? 100.0 : 120.0;

    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            const DevPanel(),
            Expanded(
              child: Stack(
                children: [
                  if (!_gamepadConnected) ...[
                    Align(
                      alignment: const Alignment(-0.98, 0.6),
                      child: EdgeStickyJoystick(
                        stickSize: stickSize,
                        knobSize: knobSize,
                        verticalOnly: true,
                        sensitivity: 0.8,
                        externalValue: Offset(0, _thrFilt / maxVal),
                        onChanged: (o) => setState(() => _throttle = o.dy.clamp(-1.0, 1.0) * maxVal),
                        onEnd: () => setState(() => _throttle = 0),
                      ),
                    ),
                    Align(
                      alignment: const Alignment(0.98, 0.6),
                      child: EdgeStickyJoystick(
                        stickSize: stickSize,
                        knobSize: knobSize,
                        verticalOnly: false,
                        sensitivity: 0.8,
                        externalValue: Offset(_steerFilt / maxVal, 0),
                        onChanged: (o) => setState(() => _steer = o.dx.clamp(-1.0, 1.0) * maxVal),
                        onEnd: () => setState(() => _steer = 0),
                      ),
                    ),
                  ],
                  GamepadStatus(
                    isConnected: _gamepadConnected,
                    throttle: _thrFilt,
                    maxVal: maxVal,
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
