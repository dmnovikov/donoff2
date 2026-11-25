// ADS1115: a conversion is started on one pass and read on the next, one channel at a time.
//   DSensorI2C -> DSensorADS1115Base -> DSensorADS1115 (the counts of the four inputs)
//                                    -> DSensorADS1115Ntc (four NTC thermistors, C)
#pragma once
#include "../dntc.h"
#include "dsensor_i2c.h"

class DSensorADS1115Base : public DSensorI2C {
 protected:
  DSensorADS1115Base(DContext& ctx, const char* name, uint32_t periodMs, uint8_t sda, uint8_t scl)
      : DSensorI2C(ctx, name, periodMs, 15, 5000, sda, scl, 0x48) {}

  virtual void store(uint8_t ch, int16_t counts) = 0;  // one input was measured

  void request() override {
    if (!ready()) return;  // found and set up in collect()
    ok_ = writeReg16(1, 0xC183 | ((uint16_t)ch_ << 12)) == 0;  // single shot, +-6.144 V, 128 SPS
  }

  bool measure() override {
    uint8_t b[2];
    if (!ok_ || !readRegs(0, b, 2)) return false;
    store(ch_, (int16_t)((b[0] << 8) | b[1]));
    ch_ = (ch_ + 1) % 4;
    return true;
  }

 private:
  uint8_t ch_ = 0;
  bool ok_ = false;
};

class DSensorADS1115 : public DSensorADS1115Base {
 public:
  DSensorADS1115(DContext& ctx, uint8_t sda, uint8_t scl, uint16_t mbReg) : DSensorADS1115Base(ctx, "ads", 1000, sda, scl) {
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
  void store(uint8_t ch, int16_t counts) override { value(ch).setRaw(counts); }
};

// Four NTC thermistors, one per input: 3.3 V - resistor - input - thermistor - GND. Values temp_ntc0..temp_ntc3
// in C; an input without a thermistor (or shorted to GND) is -127, told in the log once per change.
// Every input is measured once a second.
class DSensorADS1115Ntc : public DSensorADS1115Base {
 public:
  DSensorADS1115Ntc(DContext& ctx, uint8_t sda, uint8_t scl, uint16_t mbReg, const dntc::Params& p = dntc::Params())
      : DSensorADS1115Base(ctx, "ads", 250, sda, scl), p_(p) {
    static const char* const kNames[] = {"temp_ntc0", "temp_ntc1", "temp_ntc2", "temp_ntc3"};
    for (uint8_t i = 0; i < 4; i++) addValue(kNames[i], 100, "C", mbReg + 3 * i, 1);
  }

 protected:
  void store(uint8_t ch, int16_t counts) override {
    long t100 = 0;
    dntc::Result r = dntc::decode(dntc::adsMv(counts), p_, t100);
    if (r == dntc::Result::Ok) value(ch).setRaw(t100);
    else value(ch).fail();
    if (r == last_[ch]) return;
    last_[ch] = r;
    static const char* const kText[] = {"N:%s ok", "E:%s no thermistor", "E:%s shorted"};
    char b[48];
    snprintf(b, sizeof b, kText[(uint8_t)r], value(ch).name);
    ctx_.ev.log(b);
  }

 private:
  dntc::Params p_;
  dntc::Result last_[4] = {dntc::Result::Ok, dntc::Result::Ok, dntc::Result::Ok, dntc::Result::Ok};
};
