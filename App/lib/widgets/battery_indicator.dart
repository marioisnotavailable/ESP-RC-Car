import 'package:flutter/material.dart';

/// Displays the battery percentage received from the ESP32.
/// Shows nothing when [percent] is negative (no data yet).
class BatteryIndicator extends StatelessWidget {
  final int percent;

  const BatteryIndicator({super.key, required this.percent});

  @override
  Widget build(BuildContext context) {
    if (percent < 0) return const SizedBox.shrink();

    final color = _color(percent);

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        color: const Color.fromRGBO(0, 0, 0, 0.6),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: color, width: 1.5),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _BatteryIcon(percent: percent, color: color),
          const SizedBox(width: 6),
          Text(
            '$percent%',
            style: TextStyle(
              color: color,
              fontSize: 13,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
  }

  Color _color(int pct) {
    if (pct <= 20) return Colors.red;
    if (pct <= 40) return Colors.orange;
    return Colors.green;
  }
}

class _BatteryIcon extends StatelessWidget {
  final int percent;
  final Color color;

  const _BatteryIcon({required this.percent, required this.color});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 24,
      height: 13,
      child: CustomPaint(painter: _BatteryPainter(percent: percent, color: color)),
    );
  }
}

class _BatteryPainter extends CustomPainter {
  final int percent;
  final Color color;

  const _BatteryPainter({required this.percent, required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final bodyWidth = size.width - 3;
    final bodyRect = Rect.fromLTWH(0, 0, bodyWidth, size.height);

    // Outline
    final outlinePaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.2;
    canvas.drawRRect(
      RRect.fromRectAndRadius(bodyRect, const Radius.circular(2)),
      outlinePaint,
    );

    // Positive terminal nub
    final nubPaint = Paint()..color = color;
    canvas.drawRect(
      Rect.fromLTWH(bodyWidth, size.height * 0.3, 3, size.height * 0.4),
      nubPaint,
    );

    // Fill
    final fillWidth = (bodyWidth - 2) * (percent.clamp(0, 100) / 100.0);
    if (fillWidth > 0) {
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(1, 1, fillWidth, size.height - 2),
          const Radius.circular(1),
        ),
        Paint()..color = color,
      );
    }
  }

  @override
  bool shouldRepaint(_BatteryPainter old) =>
      old.percent != percent || old.color != color;
}
