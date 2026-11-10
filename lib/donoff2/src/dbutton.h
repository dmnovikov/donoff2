// The button of donoff: short press, long press (setup portal), very long press (reboot).
#pragma once
#include "dbase.h"

class DButton : public DBase {
 public:
  enum class Press : uint8_t { None, Short, Config, Reset };
  static constexpr uint32_t kMinMs = 25, kShortMs = 2000, kConfigMs = 6000, kResetMs = 8000, kDebounceMs = 5;

  DButton(DContext& ctx, const char* name) : DBase(ctx, name) {}

  // What happened: decided when the button is released.
  Press loop() {
    uint32_t now = ctx_.clock.ms();
    bool raw = pressedNow();
    if (raw != raw_) {
      raw_ = raw;
      changedAt_ = now;
    }
    if (raw_ == stable_ || now - changedAt_ < kDebounceMs) return Press::None;
    stable_ = raw_;
    if (stable_) {
      pressedAt_ = now;
      return Press::None;
    }
    uint32_t d = now - pressedAt_;
    if (d < kMinMs) return Press::None;
    if (d < kShortMs) return Press::Short;
    if (d < kConfigMs) return Press::Config;
    if (d >= kResetMs) return Press::Reset;
    return Press::None;
  }

 protected:
  virtual bool pressedNow() = 0;

 private:
  bool raw_ = false, stable_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0;
};
