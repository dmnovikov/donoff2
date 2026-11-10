// The status LED: an 8-step pattern, one step per 250 ms.
#pragma once
#include "dbase.h"

class DLed : public DBase {
 public:
  enum Pattern : uint8_t { Dark = 0xFF, Blink = 0x0F, Offline = 0x05 };  // bit 1 = pin high = LED dark

  DLed(DContext& ctx, const char* name) : DBase(ctx, name) {}
  void setPattern(uint8_t p) { pattern_ = p; }

  void loop() {
    uint32_t now = ctx_.clock.ms();
    if (now - last_ < kStepMs) return;
    last_ = now;
    write((pattern_ >> (step_ & 7)) & 1);
    step_++;
  }

 protected:
  static constexpr uint32_t kStepMs = 250;
  virtual void write(bool high) = 0;

 private:
  uint8_t pattern_ = Offline, step_ = 0;
  uint32_t last_ = 0;
};
