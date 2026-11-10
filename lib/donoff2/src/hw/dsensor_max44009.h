// MAX44009 (MAX44003 in donoff-lib): illuminance. A0 low gives 0x4A, high gives 0x4B.
#pragma once
#include "dsensor_i2c.h"

class DSensorMAX44009 : public DSensorI2C {
 public:
  DSensorMAX44009(DContext& ctx, uint8_t sda, uint8_t scl, uint16_t mbReg)
      : DSensorI2C(ctx, "lux", 5000, 0, 10000, sda, scl, 0x4A, 0x4B) {
    value(addValue("lux", 1, "lx", mbReg, 9)).label = "LUX";
  }

 protected:
  bool measure() override {
    uint8_t hi, lo;
    if (!readRegs(0x03, &hi, 1) || !readRegs(0x04, &lo, 1)) return false;
    uint32_t mant = ((hi & 0x0F) << 4) | (lo & 0x0F);
    value(0).set((float)((mant << (hi >> 4)) * 0.045));
    return true;
  }
};
