#pragma once
#include <Arduino.h>
#include "../drelay.h"

class DRelayGPIO : public DRelay {
 public:
  DRelayGPIO(DContext& ctx, const char* name, uint8_t pin) : DRelay(ctx, name), pin_(pin) {
    digitalWrite(pin_, LOW);
    pinMode(pin_, OUTPUT);
  }

 protected:
  void write(bool on) override { digitalWrite(pin_, on ? HIGH : LOW); }

 private:
  uint8_t pin_;
};
