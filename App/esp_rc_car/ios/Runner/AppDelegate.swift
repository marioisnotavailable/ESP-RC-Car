import Flutter
import UIKit
import GameController

@main
@objc class AppDelegate: FlutterAppDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    GeneratedPluginRegistrant.register(with: self)
    if let controller = window?.rootViewController as? FlutterViewController {
      let channel = FlutterEventChannel(name: "rc.gamepad/events", binaryMessenger: controller.binaryMessenger)
      channel.setStreamHandler(IOSGamepadStreamHandler())
    }
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }
}

// Stream handler that mirrors the Android/Windows gamepad payload:
// { connected: Bool, id: String, lx: Double (-1..1), r2: Double (0..1), l2: Double (0..1),
//   ry: Double (magnitude of right stick), isRightStickActive: Bool }
class IOSGamepadStreamHandler: NSObject, FlutterStreamHandler {
  private var sink: FlutterEventSink?
  private var observers: [NSObjectProtocol] = []
  private var ctrl: GCController?

  func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
    sink = events
    // Observe controller connect/disconnect
    let nc = NotificationCenter.default
    observers.append(nc.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
      self?.pickControllerAndStart()
      self?.push()
    })
    observers.append(nc.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
      self?.ctrl = nil
      self?.push()
    })
    // Discover wireless controllers briefly
    GCController.startWirelessControllerDiscovery(completionHandler: nil)
    pickControllerAndStart()
    push()
    return nil
  }

  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    for o in observers { NotificationCenter.default.removeObserver(o) }
    observers.removeAll()
    ctrl?.extendedGamepad?.valueChangedHandler = nil
    ctrl = nil
    sink = nil
    return nil
  }

  private func pickControllerAndStart() {
    // Pick first connected controller
    if let first = GCController.controllers().first {
      ctrl = first
      if let gp = first.extendedGamepad {
        gp.valueChangedHandler = { [weak self] _, _ in
          self?.push()
        }
      }
    } else {
      ctrl = nil
    }
  }

  private func push() {
    guard let sink = sink else { return }
    let connected = !GCController.controllers().isEmpty
    let id = ctrl?.vendorName ?? "Gamepad"

    var lx = 0.0
    var l2 = 0.0
    var r2 = 0.0
    var rx = 0.0
    var ry = 0.0
    if let gp = ctrl?.extendedGamepad {
      // GCControllerDirectionPad doesn't expose `x`/`y` directly; use xAxis/yAxis.value
      lx = Double(gp.leftThumbstick.xAxis.value)
      // Amplify triggers (×4) to match Android mapping; clamp to 0..1
      l2 = min(1.0, max(0.0, Double(gp.leftTrigger.value) * 4.0))
      r2 = min(1.0, max(0.0, Double(gp.rightTrigger.value) * 4.0))
      rx = Double(gp.rightThumbstick.xAxis.value)
      ry = Double(gp.rightThumbstick.yAxis.value)
    }
    let rightMag = max(abs(rx), abs(ry))
    let payload: [String: Any] = [
      "connected": connected,
      "id": id,
      "lx": lx,
      "r2": r2,
      "l2": l2,
      "ry": rightMag,
      "isRightStickActive": rightMag > 0.1
    ]
    sink(payload)
  }
}
