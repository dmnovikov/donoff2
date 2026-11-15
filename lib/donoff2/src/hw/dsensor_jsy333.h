// JSY-MK-333 three phase meter: the port and the address come from the settings (rtubaud, rtuparity, rtustop, rtuaddr).
#pragma once
#include "dsensor_rtu.h"
#include "../djsy333.h"

class DSensorJSY333 : public DSensorRTU {
 public:
  // the table: U A..C, I A..C, P A..C, P total, PF A..C, PF total, frequency, energy
  enum { U = 0, I = 3, P = 6, PTotal = 9, Pf = 10, PfTotal = 13, Freq = 14, Energy = 15 };

  DSensorJSY333(DContext& ctx, const Port& port, uint8_t addr, uint16_t mbReg)
      : DSensorRTU(ctx, "jsy3", 2000, port, addr, djsy333::kFc, djsy333::kStart, djsy333::kRegs, 600) {
    // 600 ms for the answer: 107 bytes take 112 ms at 9600, and the meter answers late (200 ms lost every other one)
    static const char* kU[] = {"jsy3_ua", "jsy3_ub", "jsy3_uc"};
    static const char* kI[] = {"jsy3_ia", "jsy3_ib", "jsy3_ic"};
    static const char* kP[] = {"jsy3_pa", "jsy3_pb", "jsy3_pc"};
    static const char* kPf[] = {"jsy3_pfa", "jsy3_pfb", "jsy3_pfc"};
    uint16_t reg = mbReg;
    for (auto* n : kU) add(n, 100, "V", reg, 16);
    for (auto* n : kI) add(n, 100, "A", reg, 17);
    for (auto* n : kP) add(n, 1, "W", reg, 18);
    add("jsy3_p", 1, "W", reg, 18);
    for (auto* n : kPf) add(n, 1000, "", reg, 20);
    add("jsy3_pf", 1000, "", reg, 20);
    add("jsy3_freq", 100, "Hz", reg, 21);
    add("jsy3_energy", 100, "kWh", reg, 19);
    setGroup("jsy3_json");
  }

 protected:
  void decode(const uint16_t* regs) override {
    djsy333::Reading r;
    djsy333::decode(regs, r);
    for (int k = 0; k < 3; k++) {
      value(U + k).setRaw(r.u100[k]);
      value(I + k).setRaw(r.i100[k]);
      value(P + k).setRaw(r.p[k]);
      value(Pf + k).setRaw(r.pf1000[k]);
    }
    value(PTotal).setRaw(r.pTotal);
    value(PfTotal).setRaw(r.pfTotal1000);
    value(Freq).setRaw(r.freq100);
    value(Energy).setRaw(r.energy100);
  }

 private:
  void add(const char* n, long mult, const char* unit, uint16_t& reg, uint8_t type) {
    addValue(n, mult, unit, reg, type);
    reg += 3;
  }
};
