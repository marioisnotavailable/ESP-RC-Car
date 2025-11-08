import 'package:flutter/material.dart';

/// A wrapper that increases the hit area and limits the visible joystick size so
/// the knob remains reachable when pinned to the screen edge.
class EdgeStickyJoystick extends StatelessWidget {
  final double stickSize;
  final double knobSize;
  final bool verticalOnly;
  final double sensitivity;
  
  final Offset externalValue;
  final ValueChanged<Offset>? onChanged;
  final VoidCallback? onEnd;

  const EdgeStickyJoystick({
    super.key,
    required this.stickSize,
    required this.knobSize,
    required this.verticalOnly,
    required this.externalValue,
    required this.sensitivity,
    this.onChanged,
    this.onEnd,
  });

  @override
  Widget build(BuildContext context) {
    // Make a larger invisible hit area so the user can reach the stick closer to
    // the screen edge. The visible circular joystick remains the same size.
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 6.0),
      child: SizedBox(
        width: stickSize * 1.05,
        height: stickSize * 1.05,
        child: Center(
          child: Joystick(
            size: stickSize,
            knobSize: knobSize,
            verticalOnly: verticalOnly,
            externalValue: externalValue,
            sensitivity: sensitivity,
            onChanged: onChanged,
            onEnd: onEnd,
          ),
        ),
      ),
    );
  }
}

/// A circular joystick widget with a draggable knob.
class Joystick extends StatefulWidget {
  final double size;
  final double knobSize;
  final bool verticalOnly;
  final Offset externalValue; // Normalized input (-1.0 to 1.0)
  final double sensitivity;
  final ValueChanged<Offset>? onChanged;
  final VoidCallback? onEnd;

  const Joystick({
    super.key,
    required this.size,
    required this.knobSize,
    required this.verticalOnly,
    required this.externalValue,
    this.sensitivity = 0.8,
    this.onChanged,
    this.onEnd,
  });

  @override
  State<Joystick> createState() => _JoystickState();
}

class _JoystickState extends State<Joystick> {
  Offset _local = Offset.zero; // Local normalized offset (-1.0 to 1.0)
  bool _isDragging = false;

  // Sensitivity determines how far the user must drag to reach the maximum value.
  // A value of 0.6 means only 60% of the radius needs to be covered.
  late double _sensitivity;
  Offset? _pointerOffset;

  @override
  void initState() {
    super.initState();
    _sensitivity = widget.sensitivity.clamp(0.1, 1.0);
  }

  double get _radius => widget.size / 2;
  double get _travelRadius => _radius - (widget.knobSize / 2);

  @override
  void didUpdateWidget(covariant Joystick oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!_isDragging) {
      _local = _clampNorm(widget.externalValue);
    }
  }

  void _handlePanDown(DragDownDetails details) {
    _isDragging = true;
    // Calculate the offset between the touch point and the knob's center
    // to prevent the knob from jumping to the touch position.
    final p = details.localPosition;
    final center = Offset(widget.size / 2, widget.size / 2);
    final knobCenter = center + Offset(_local.dx * _travelRadius, -_local.dy * _travelRadius);
    _pointerOffset = p - knobCenter;
    _updatePosition(details.localPosition);
  }

  void _handlePanUpdate(DragUpdateDetails details) {
    _updatePosition(details.localPosition);
  }
  
  void _updatePosition(Offset localPosition) {
    final desiredKnobCenter = localPosition - (_pointerOffset ?? Offset.zero);
    final center = Offset(widget.size / 2, widget.size / 2);
    final vector = desiredKnobCenter - center;
    
    var norm = Offset(vector.dx / (_radius * _sensitivity), -vector.dy / (_radius * _sensitivity));
    
    if (widget.verticalOnly) {
      norm = Offset(0, norm.dy);
    }

    final clamped = _clampNorm(norm);

    setState(() => _local = clamped);
    widget.onChanged?.call(_local);
  }

  void _handlePanEnd(DragEndDetails details) {
    _isDragging = false;
    _pointerOffset = null;
    setState(() => _local = Offset.zero);
    widget.onEnd?.call();
  }

  @override
  Widget build(BuildContext context) {
    final knobOffset = _isDragging ? _local : _clampNorm(widget.externalValue);
    final px = knobOffset.dx * _travelRadius;
    final py = -knobOffset.dy * _travelRadius;

    return GestureDetector(
      onPanDown: _handlePanDown,
      onPanUpdate: _handlePanUpdate,
      onPanEnd: _handlePanEnd,
      child: Container(
        width: widget.size,
        height: widget.size,
        decoration: BoxDecoration(
          color: const Color(0xFF1F1F1F),
          shape: BoxShape.circle,
          boxShadow: [
            BoxShadow(
              color: Colors.white.withOpacity(0.05),
              blurRadius: 20,
              spreadRadius: -5,
            ),
          ],
        ),
        child: Center(
          child: Transform.translate(
            offset: Offset(px, py),
            child: Container(
              width: widget.knobSize,
              height: widget.knobSize,
              decoration: const BoxDecoration(
                color: Color(0xFF7A7A7A),
                shape: BoxShape.circle,
                boxShadow: [
                  BoxShadow(blurRadius: 18, offset: Offset(0, 8), color: Color(0x59000000)),
                  BoxShadow(blurRadius: 10, offset: Offset(0, 0), color: Color(0x14FFFFFF)),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Offset _clampNorm(Offset o) =>
      Offset(o.dx.clamp(-1.0, 1.0), o.dy.clamp(-1.0, 1.0));
}
