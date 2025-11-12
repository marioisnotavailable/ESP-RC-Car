import 'package:flutter/material.dart';

class GamepadStatus extends StatelessWidget {
  final bool isConnected;
  final double throttle;
  final int maxVal;

  const GamepadStatus({
    super.key,
    required this.isConnected,
    required this.throttle,
    required this.maxVal,
  });

  @override
  Widget build(BuildContext context) {
    if (!isConnected) {
      return const SizedBox.shrink();
    }

    return Align(
      alignment: const Alignment(0.92, -0.92),
      child: Container(
        constraints: const BoxConstraints(maxWidth: 280),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        decoration: BoxDecoration(
          color: const Color.fromRGBO(0, 0, 0, 0.6),
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: Colors.green, width: 2),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            const Text(
              '🎮 GAMEPAD ACTIVE',
              style: TextStyle(
                color: Colors.green,
                fontSize: 14,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 2),
            const Text(
              'Left Stick: Steer\nL2/R2 Triggers: Throttle/Brake',
              textAlign: TextAlign.center,
              style: TextStyle(color: Color(0xFF99AADD), fontSize: 11),
            ),
            const SizedBox(height: 4),
            _buildThrottleIndicator(),
          ],
        ),
      ),
    );
  }

  Widget _buildThrottleIndicator() {
    final r2Value = (throttle > 0) ? (throttle / maxVal * 100).toInt() : 0;
    final l2Value = (throttle < 0) ? (-throttle / maxVal * 100).toInt() : 0;
    final gasColor =
        throttle.abs() > 50
            ? (throttle > 0 ? Colors.green : Colors.red)
            : const Color(0xFFBBCCDD);

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          'L2: $l2Value% | R2: $r2Value%',
          textAlign: TextAlign.center,
          style: const TextStyle(color: Color(0xFFBBCCDD), fontSize: 10),
        ),
        const SizedBox(height: 2),
        Text(
          'Throttle: ${throttle.toInt()}',
          textAlign: TextAlign.center,
          style: TextStyle(
            color: gasColor,
            fontSize: 11,
            fontWeight: FontWeight.bold,
          ),
        ),
      ],
    );
  }
}
