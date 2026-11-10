#pragma once
#include <Arduino.h>
#include "../dbutton.h"

class DButtonGPIO : public DButton {
 public:
  DButtonGPIO(DContext& ctx, uint8_t pin) : DButton(ctx, "button"), pin_(pin) {}
  void begin() { pinMode(pin_, INPUT_PULLUP); }

 protected:
  bool pressedNow() override { return digitalRead(pin_) == LOW; }

 private:
  uint8_t pin_;
};
