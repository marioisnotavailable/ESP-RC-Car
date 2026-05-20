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

class IOSGamepadStreamHandler: NSObject, FlutterStreamHandler {
  private var eventSink: FlutterEventSink?
  private var notificationObservers: [NSObjectProtocol] = []
  private var activeController: GCController?

  // Throttle reportStatus to ~50Hz to match Dart send loop and avoid bridge thrash.
  private static let minReportIntervalSec: TimeInterval = 1.0 / 50.0
  private var lastReportTime: TimeInterval = 0

  func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
    self.eventSink = events
    setupControllerObservers()
    discoverAndAssignController()
    reportStatus(force: true)
    return nil
  }

  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    teardownControllerObservers()
    GCController.stopWirelessControllerDiscovery()
    eventSink = nil
    return nil
  }

  private func setupControllerObservers() {
    let center = NotificationCenter.default
    notificationObservers.append(
      center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
        self?.discoverAndAssignController()
        self?.reportStatus(force: true)
      }
    )
    notificationObservers.append(
      center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
        self?.discoverAndAssignController()
        self?.reportStatus(force: true)
      }
    )
  }

  private func teardownControllerObservers() {
    for observer in notificationObservers {
      NotificationCenter.default.removeObserver(observer)
    }
    notificationObservers.removeAll()
    activeController?.extendedGamepad?.valueChangedHandler = nil
    activeController = nil
  }

  private func discoverAndAssignController() {
    // Clear handler on previously active controller before swapping.
    activeController?.extendedGamepad?.valueChangedHandler = nil

    // Pick first controller that actually exposes an extended gamepad
    // (skips Siri Remote, headsets, etc.).
    activeController = GCController.controllers().first { $0.extendedGamepad != nil }

    if let gamepad = activeController?.extendedGamepad {
      gamepad.valueChangedHandler = { [weak self] _, _ in
        self?.reportStatus()
      }
      // Stop discovery once we have a usable controller.
      GCController.stopWirelessControllerDiscovery()
    } else {
      GCController.startWirelessControllerDiscovery {}
    }
  }

  private func reportStatus(force: Bool = false) {
    guard let sink = eventSink else { return }

    let now = CACurrentMediaTime()
    if !force && (now - lastReportTime) < IOSGamepadStreamHandler.minReportIntervalSec {
      return
    }
    lastReportTime = now

    let isConnected = activeController != nil
    let gamepadId = activeController?.vendorName ?? ""

    var lx = 0.0
    var l2 = 0.0
    var r2 = 0.0
    var buttonA = false
    var buttonB = false

    if let gamepad = activeController?.extendedGamepad {
      lx = Double(gamepad.leftThumbstick.xAxis.value)
      l2 = Double(gamepad.leftTrigger.value)
      r2 = Double(gamepad.rightTrigger.value)
      buttonA = gamepad.buttonA.isPressed
      buttonB = gamepad.buttonB.isPressed
    }

    let payload: [String: Any] = [
      "connected": isConnected,
      "id": gamepadId,
      "lx": lx,
      "r2": r2,
      "l2": l2,
      "buttonA": buttonA,
      "buttonB": buttonB,
    ]
    sink(payload)
  }
}
