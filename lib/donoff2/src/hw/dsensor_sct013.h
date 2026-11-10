// SCT013 current clamps on the ADC. The calculation is that of EmonLib, the calibration of donoff (31.24).
#pragma once
#include <math.h>
#include "dplatform.h"
#include "../dsensor.h"

// Measures one channel; remembers the DC offset of that channel.
class DRmsMeter {
 public:
  static constexpr int kSamples = 1480;
  float measure(uint8_t pin) {
    double sum = 0;
    for (int n = 0; n < kSamples; n++) {
      int v = analogRead(pin);
      offset_ += (v - offset_) / DONOFF_ADC_MAX;
      double f = v - offset_;
      sum += f * f;
      if (n % 200 == 0) yield();  // 1480 readings take over 100 ms; Wi-Fi must breathe
    }
    return (float)(31.24 * (3.3 / DONOFF_ADC_MAX) * sqrt(sum / kSamples));
  }

 private:
  double offset_ = DONOFF_ADC_MAX / 2;
};

class DSensorSCT013 : public DSensor {
 public:
  DSensorSCT013(DContext& ctx, const char* valueName, uint8_t pin, uint16_t mbReg = 0, const char* label = "SCT013_1")
      : DSensor(ctx, valueName, 2000, 0, kWarmMs), pin_(pin) {
    addValue(valueName, 100, "A", mbReg, 2);
    value(0).label = label;
  }

 protected:
  void warmup() override { meter_.measure(pin_); }

  bool collect() override {
    // A floating input reads the top of the scale: no clamp is plugged in.
    int a = analogRead(pin_);
    bool absent = a >= DONOFF_ADC_MAX - 2;
    if (wasAbsent_ && !absent) {
      wasAbsent_ = false;
      value(0).notReady();
      restartDelay();  // plugged in just now: the offset needs time
      return true;
    }
    wasAbsent_ = absent;
    if (absent) return false;
    value(0).set(meter_.measure(pin_));
    return true;
  }

 private:
  static constexpr uint32_t kWarmMs = 30000;
  uint8_t pin_;
  DRmsMeter meter_;
  bool wasAbsent_ = false;
};

// Three clamps through an analog switch, one channel per pass, plus their sum.
class DSensorSCT013x3 : public DSensor {
 public:
  DSensorSCT013x3(DContext& ctx, uint8_t pinA, uint8_t pinB, uint8_t adc, uint16_t mbReg)
      : DSensor(ctx, "sct013x3", 2000), a_(pinA), b_(pinB), adc_(adc) {
    static const char* const kNames[] = {"sct013_1", "sct013_2", "sct013_3", "sct013x3"};
    for (uint8_t i = 0; i < 4; i++) addValue(kNames[i], 100, "A", mbReg + 3 * i, 2);
  }

 protected:
  void onBegin() override {
    started_ = ctx_.clock.ms();
    pinMode(a_, OUTPUT);
    pinMode(b_, OUTPUT);
    select(0);
  }

  bool collect() override {
    uint32_t now = ctx_.clock.ms();
    if (now - started_ >= kWarmMs) value(ch_).set(meter_[ch_].measure(adc_));
    else meter_[ch_].measure(adc_);
    if (value(0).ok() && value(1).ok() && value(2).ok()) value(3).setRaw(value(0).raw + value(1).raw + value(2).raw);
    select((ch_ + 1) % 3);
    return true;
  }

 private:
  static constexpr uint32_t kWarmMs = 30000;
  void select(uint8_t c) {
    ch_ = c;
    digitalWrite(a_, c == 1);
    digitalWrite(b_, c == 2);
  }
  uint8_t a_, b_, adc_, ch_ = 0;
  DRmsMeter meter_[3];
  uint32_t started_ = 0;
};
