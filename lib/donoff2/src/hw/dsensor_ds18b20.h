#pragma once
#include <DallasTemperature.h>
#include <OneWire.h>
#include "../dsensor.h"

class DSensorDS18B20 : public DSensor {
 public:
  DSensorDS18B20(DContext& ctx, const char* valueName, uint8_t pin, uint16_t mbReg = 0)
      : DSensor(ctx, valueName, 5000, 800), wire_(pin), ds_(&wire_) {
    addValue(valueName, 100, "C", mbReg);
  }

 protected:
  void onBegin() override {
    ds_.begin();
    ds_.setWaitForConversion(false);
  }
  void request() override {
    if (ds_.getDeviceCount() == 0) onBegin();  // plugged in after the start
    ds_.requestTemperatures();
  }
  bool collect() override {
    float t = ds_.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C || t == 85.0f) return false;  // 85 is the power-on value
    value(0).set(t);
    return true;
  }

 private:
  OneWire wire_;
  DallasTemperature ds_;
};
