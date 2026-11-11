// PZEM-004T v3: 9600 8N1, address 0xF8, 10 input registers from 0.
#pragma once
#include "dsensor_rtu.h"
#include "../dpzem.h"

class DSensorPZEM : public DSensorRTU {
 public:
  enum { Current, Power, Voltage, Energy };

  DSensorPZEM(DContext& ctx, uint8_t rx, uint8_t tx, uint16_t mbReg)
      : DSensorRTU(ctx, "pzem", 5000, Port{rx, tx, 9600, 0, 1}, dpzem::kAddr, dpzem::kFc, 0, dpzem::kRegs) {
    add("pzem_current", "A", mbReg, 11, "PZEM04_Current");
    add("pzem_power", "W", mbReg + 3, 12, "PZEM04_Power");
    add("pzem_voltage", "V", mbReg + 6, 13, "PZEM04_Voltage");
    add("pzem_energy", "kWh", mbReg + 9, 14, "PZEM04_Energy");
    value(Current).key = "current";
    value(Power).key = "power";
    value(Voltage).key = "volage";  // sic: the old devices send it so, the readers expect it
    value(Energy).key = "energy";
    setGroup("pzem004");
  }

 protected:
  void decode(const uint16_t* regs) override {
    dpzem::Reading r;
    dpzem::decode(regs, r);
    value(Current).set(r.current);
    value(Power).set(r.power);
    value(Voltage).set(r.voltage);
    value(Energy).set(r.energy);
  }

 private:
  void add(const char* n, const char* unit, uint16_t reg, uint8_t type, const char* label) {
    value(addValue(n, 100, unit, reg, type)).label = label;
  }
};
