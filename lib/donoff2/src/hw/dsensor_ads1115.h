// ADS1115: a conversion is started on one pass and read on the next, one channel at a time.
#pragma once
#include "dsensor_i2c.h"

class DSensorADS1115 : public DSensorI2C {
 public:
  DSensorADS1115(DContext& ctx, uint8_t sda, uint8_t scl, uint16_t mbReg)
      : DSensorI2C(ctx, "ads", 1000, 15, 5000, sda, scl, 0x48) {
    static const char* const kNames[] = {"ads0", "ads1", "ads2", "ads3"};
    static const char* const kKeys[] = {"ch0", "ch1", "ch2", "ch3"};
    static const char* const kLabels[] = {"ADSCHANNEL0", "ADSCHANNEL1", "ADSCHANNEL2", "ADSCHANNEL3"};
    for (uint8_t i = 0; i < 4; i++) {
      DValue& v = value(addValue(kNames[i], 1, "", mbReg + 3 * i, 10));
      v.key = kKeys[i];
      v.label = kLabels[i];
    }
    setGroup("ads_json");
  }

 protected:
  void request() override {
    if (!ready()) return;  // found and set up in collect()
    ok_ = writeReg16(1, 0xC183 | ((uint16_t)ch_ << 12)) == 0;  // single shot, +-6.144 V, 128 SPS
  }

  bool measure() override {
    uint8_t b[2];
    if (!ok_ || !readRegs(0, b, 2)) return false;
    value(ch_).setRaw((int16_t)((b[0] << 8) | b[1]));
    ch_ = (ch_ + 1) % 4;
    return true;
  }

 private:
  uint8_t ch_ = 0;
  bool ok_ = false;
};
