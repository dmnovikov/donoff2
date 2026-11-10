// BH1750: illuminance. It measures by itself, one reading per pass. ADDR low gives 0x23, high gives 0x5C.
#pragma once
#include "dsensor_i2c.h"

class DSensorBH1750 : public DSensorI2C {
 public:
  DSensorBH1750(DContext& ctx, uint8_t sda, uint8_t scl, uint16_t mbReg)
      : DSensorI2C(ctx, "lux", 5000, 0, 0, sda, scl, 0x23, 0x5C) {
    value(addValue("lux", 1, "lx", mbReg, 9)).label = "BH1750";
  }

 protected:
  uint8_t setupChip() override {
    uint8_t e = command(0x01);           // power on
    return e ? e : command(0x10);        // continuous, 1 lx
  }
  bool measure() override {
    uint8_t b[2];
    if (!readRaw(b, 2)) return false;
    value(0).set(((b[0] << 8) | b[1]) / 1.2f);
    return true;
  }
};
