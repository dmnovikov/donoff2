// JSY-MK-163 / JSY1003F meter: the port and the address come from the settings (rtubaud, rtuparity, rtustop, rtuaddr).
#pragma once
#include "dsensor_rtu.h"
#include "../djsy.h"

class DSensorJSY : public DSensorRTU {
 public:
  enum { Voltage, Current, Power, Energy, Pf, Freq };

  DSensorJSY(DContext& ctx, const Port& port, uint8_t addr, uint16_t mbReg)
      : DSensorRTU(ctx, "jsy", 5000, port, addr, djsy::kFc, djsy::kStart, djsy::kRegs) {
    add("jsy_voltage", 100, "V", mbReg, 16, "voltage");
    add("jsy_current", 1000, "A", mbReg + 3, 17, "current");
    add("jsy_power", 10, "W", mbReg + 6, 18, "power");
    add("jsy_energy", 1000, "kWh", mbReg + 9, 19, "energy");
    add("jsy_pf", 1000, "", mbReg + 12, 20, "pf");
    add("jsy_freq", 100, "Hz", mbReg + 15, 21, "freq");
    setGroup("jsy_json");
  }

 protected:
  void decode(const uint16_t* regs) override {
    djsy::Reading r;
    djsy::decode(regs, r);
    value(Voltage).setRaw(r.voltage100);
    value(Current).setRaw(r.current1000);
    value(Power).setRaw(r.power10);
    value(Energy).set(r.energy);
    value(Pf).setRaw(r.pf1000);
    value(Freq).setRaw(r.freq100);
  }

 private:
  void add(const char* n, long mult, const char* unit, uint16_t reg, uint8_t type, const char* key) {
    value(addValue(n, mult, unit, reg, type)).key = key;
  }
};
