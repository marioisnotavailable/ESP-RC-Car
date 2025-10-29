#include "gamepad_channel.h"

#include <flutter/binary_messenger.h>

#include <Windows.h>
#include <Xinput.h>

#include <chrono>

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;

std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> GamepadChannel::channel_;
std::unique_ptr<flutter::StreamHandlerFunctions<flutter::EncodableValue>> GamepadChannel::handler_;
std::atomic<bool> GamepadChannel::streaming_{false};
std::thread GamepadChannel::poller_;

namespace {
// Normalize helper
inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct State {
  bool connected{false};
  double lx{0};
  double l2{0};
  double r2{0};
  double ry_abs{0};
  double rx_abs{0};
  std::string id{""};
};

State ReadXInput() {
  State s;
  // Iterate controllers 0..3 and pick the first connected
  for (DWORD i = 0; i < 4; ++i) {
    XINPUT_STATE xi{};
    ZeroMemory(&xi, sizeof(XINPUT_STATE));
    if (XInputGetState(i, &xi) == ERROR_SUCCESS) {
      s.connected = true;
      const auto& g = xi.Gamepad;
      // Left stick X in -1..1 (account for deadzone if desired)
      const double normLX = (g.sThumbLX >= 0 ? (g.sThumbLX / 32767.0) : (g.sThumbLX / 32768.0));
      s.lx = clamp(normLX, -1.0, 1.0);
      // Triggers 0..255 -> 0..1
      // Amplify triggers slightly to match Android behavior (some controllers report low values)
      s.l2 = clamp((g.bLeftTrigger / 255.0) * 4.0, 0.0, 1.0);
      s.r2 = clamp((g.bRightTrigger / 255.0) * 4.0, 0.0, 1.0);
      // Right stick activity (abs of Y)
      const double normRY = (g.sThumbRY >= 0 ? (g.sThumbRY / 32767.0) : (g.sThumbRY / 32768.0));
      const double normRX = (g.sThumbRX >= 0 ? (g.sThumbRX / 32767.0) : (g.sThumbRX / 32768.0));
      s.ry_abs = std::abs(normRY);
      s.rx_abs = std::abs(normRX);
      s.id = std::string("XInput") + std::to_string(i);
      break;
    }
  }
  return s;
}
}

void GamepadChannel::Register(flutter::BinaryMessenger* messenger) {
  if (channel_) return;
  channel_ = std::make_unique<flutter::EventChannel<EncodableValue>>(
      messenger, "rc.gamepad/events", &flutter::StandardMethodCodec::GetInstance());

  handler_ = std::make_unique<flutter::StreamHandlerFunctions<EncodableValue>>(
      flutter::StreamHandlerFunctions<EncodableValue>{
          // onListen
          .on_listen = [](const EncodableValue* /*args*/, std::unique_ptr<flutter::EventSink<EncodableValue>>&& sink)
              -> std::unique_ptr<flutter::StreamHandlerError<EncodableValue>> {
            streaming_.store(true);
            // Start poller thread at ~50 Hz
            poller_ = std::thread([sink_ptr = std::move(sink)]() mutable {
              State last{};
              auto sink = std::move(sink_ptr);
              while (streaming_.load()) {
                State cur = ReadXInput();
                // Build map
                EncodableMap map;
                map[EncodableValue("connected")] = EncodableValue(cur.connected);
                map[EncodableValue("id")] = EncodableValue(cur.id);
                map[EncodableValue("lx")] = EncodableValue(cur.lx);
                // Strict: only triggers control throttle
                map[EncodableValue("r2")] = EncodableValue(cur.r2);
                map[EncodableValue("l2")] = EncodableValue(cur.l2);
                // Provide right-stick magnitude (max of X/Y) for parity with Android debug info
                const double rightMag = std::max(cur.ry_abs, cur.rx_abs);
                map[EncodableValue("ry")] = EncodableValue(rightMag);
                map[EncodableValue("isRightStickActive")] = EncodableValue(rightMag > 0.1);

                try {
                  sink->Success(EncodableValue(map));
                } catch (...) {
                  // Ignore send errors
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(20));
              }
            });
            return nullptr;
          },
          // onCancel
          .on_cancel = [](const EncodableValue* /*args*/)
              -> std::unique_ptr<flutter::StreamHandlerError<EncodableValue>> {
            streaming_.store(false);
            if (poller_.joinable()) poller_.join();
            return nullptr;
          },
      });

  channel_->SetStreamHandler(std::move(handler_));
}

void GamepadChannel::Shutdown() {
  streaming_.store(false);
  if (poller_.joinable()) poller_.join();
  handler_.reset();
  channel_.reset();
}
