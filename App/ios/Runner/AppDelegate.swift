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

  func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
    self.eventSink = events
    setupControllerObservers()
    discoverAndAssignController()
    reportStatus()
    return nil
  }

  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    teardownControllerObservers()
    eventSink = nil
    return nil
  }

  private func setupControllerObservers() {
    let center = NotificationCenter.default
    notificationObservers.append(
      center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
        self?.discoverAndAssignController()
        self?.reportStatus()
      }
    )
    notificationObservers.append(
      center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
        self?.discoverAndAssignController()
        self?.reportStatus()
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
    // Stop any previous discovery process
    GCController.stopWirelessControllerDiscovery()
    
    // Assign the first connected controller
    activeController = GCController.controllers().first
    
    if let controller = activeController, let gamepad = controller.extendedGamepad {
      gamepad.valueChangedHandler = { [weak self] _, _ in
        self?.reportStatus()
      }
    } else {
      // If no controller is found, start discovery
      GCController.startWirelessControllerDiscovery {}
    }
  }

  private func reportStatus() {
    guard let sink = eventSink else { return }

    let isConnected = activeController != nil
    let gamepadId = activeController?.vendorName ?? ""
    
    var lx = 0.0
    var l2 = 0.0
    var r2 = 0.0

    if let gamepad = activeController?.extendedGamepad {
      lx = Double(gamepad.leftThumbstick.xAxis.value)
      l2 = Double(gamepad.leftTrigger.value)
      r2 = Double(gamepad.rightTrigger.value)
    }

    let payload: [String: Any] = [
      "connected": isConnected,
      "id": gamepadId,
      "lx": lx,
      "r2": r2,
      "l2": l2,
    ]
    sink(payload)
  }
}
