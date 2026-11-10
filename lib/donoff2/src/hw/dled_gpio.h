#pragma once
#include <Arduino.h>
#include "../dled.h"

class DLedGPIO : public DLed {
 public:
  DLedGPIO(DContext& ctx, uint8_t pin) : DLed(ctx, "led"), pin_(pin) {}
  void begin() { pinMode(pin_, OUTPUT); }

 protected:
  void write(bool high) override { digitalWrite(pin_, high); }

 private:
  uint8_t pin_;
};
