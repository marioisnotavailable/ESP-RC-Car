import 'dart:async';
import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart';
import 'package:esp_rc_car/connection_service.dart';

class ControllerService extends ChangeNotifier {
  final ConnectionService _connectionService;

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

  double get steer => _steerFilt;
  double get throttle => _thrFilt;

  // Gamepad
  static const _gamepadChannel = EventChannel('rc.gamepad/events');
  StreamSubscription? _gpSub;
  bool _gamepadConnected = false;
  bool get gamepadConnected => _gamepadConnected;

  // Main Loop
  Timer? _loop;
  static const int sendHz = 50;

  ControllerService(this._connectionService) {
    _subscribeToGamepad();
    _startControlLoop();
  }

  @override
  void dispose() {
    _loop?.cancel();
    _gpSub?.cancel();
    super.dispose();
  }

  void setControls(double steer, double throttle) {
    _steer = steer;
    _throttle = throttle;
  }

  void setSteer(double value) {
    _steer = value * maxVal;
  }

  void setThrottle(double value) {
    _throttle = value * maxVal;
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
    _gpSub = _gamepadChannel.receiveBroadcastStream().listen(
      (event) {
        if (event is! Map) return;

        final bool connected = event['connected'] ?? false;
        if (_gamepadConnected != connected) {
          _gamepadConnected = connected;
          notifyListeners();
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
      },
      onError: (_) {
        if (_gamepadConnected) {
          _gamepadConnected = false;
          notifyListeners();
        }
      },
    );
  }

  void _startControlLoop() {
    const changeThreshold = 0.5;

    _loop = Timer.periodic(Duration(milliseconds: (1000 / sendHz).round()), (
      _,
    ) {
      final filteredSteer =
          steerFilterAlpha * _steerFilt + (1 - steerFilterAlpha) * _steer;
      final filteredThrottle =
          throttleFilterAlpha * _thrFilt +
          (1 - throttleFilterAlpha) * _throttle;

      final clampedSteer =
          filteredSteer.clamp(-maxVal.toDouble(), maxVal.toDouble());
      final clampedThrottle =
          filteredThrottle.clamp(-maxVal.toDouble(), maxVal.toDouble());

      final steerChanged =
          (clampedSteer - _steerFilt).abs() > changeThreshold;
      final throttleChanged =
          (clampedThrottle - _thrFilt).abs() > changeThreshold;

      _steerFilt = clampedSteer;
      _thrFilt = clampedThrottle;

      _sendControls();

      if (steerChanged || throttleChanged) {
        notifyListeners();
      }
    });
  }
}
