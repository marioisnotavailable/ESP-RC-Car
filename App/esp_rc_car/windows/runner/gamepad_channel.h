#pragma once

#include <flutter/event_channel.h>
#include <flutter/standard_method_codec.h>

#include <atomic>
#include <memory>
#include <thread>

class GamepadChannel {
 public:
  static void Register(flutter::BinaryMessenger* messenger);
  static void Shutdown();

 private:
  static std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> channel_;
  static std::unique_ptr<flutter::StreamHandler<flutter::EncodableValue>> handler_;
  static std::atomic<bool> streaming_;
  static std::thread poller_;
};
