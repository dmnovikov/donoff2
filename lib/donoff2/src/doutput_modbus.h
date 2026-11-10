// Modbus, read only. This class knows the map and answers a PDU; TCP or RTU is the framing of a subclass.
//   Registers (FC03, FC04): 1 time state (0 none, 1 synced, 2 stale), 2-3 uptime, seconds;
//     a value at mbReg: mbReg high word, mbReg+1 low word (signed, raw = value * mult),
//     mbReg+2 state (0 not ready, 1 ok, 2 error).
//   Bits (FC01, FC02): relay i at address i. Anything else, every write included: exception.
#pragma once
#include "doutput.h"

class DOutputModbus : public DOutput {
 public:
  using DOutput::DOutput;

  // Answer to a PDU; returns its length.
  size_t answer(const uint8_t* q, size_t n, uint8_t* r) {
    uint8_t fc = n ? q[0] : 0;
    if (n != 5 || fc < 1 || fc > 4) return error(fc, 1, r);
    uint16_t start = (uint16_t)(q[1] << 8) | q[2], cnt = (uint16_t)(q[3] << 8) | q[4];
    if (fc <= 2) {
      if (cnt == 0 || cnt > 2000) return error(fc, 3, r);
      uint8_t bytes = (cnt + 7) / 8;
      for (uint8_t i = 0; i < bytes; i++) r[2 + i] = 0;
      for (uint16_t i = 0; i < cnt; i++) {
        uint32_t a = (uint32_t)start + i;
        if (a >= dev_.relays().size()) return error(fc, 2, r);
        if (dev_.relays()[a]->isOn()) r[2 + i / 8] |= 1 << (i % 8);
      }
      r[0] = fc;
      r[1] = bytes;
      return 2u + bytes;
    }
    if (cnt == 0 || cnt > 125) return error(fc, 3, r);
    for (uint16_t i = 0; i < cnt; i++) {
      uint16_t v;
      if (!reg((uint32_t)start + i, v)) return error(fc, 2, r);
      r[2 + 2 * i] = v >> 8;
      r[3 + 2 * i] = (uint8_t)v;
    }
    r[0] = fc;
    r[1] = (uint8_t)(2 * cnt);
    return 2u + 2u * cnt;
  }

 private:
  static size_t error(uint8_t fc, uint8_t code, uint8_t* r) {
    r[0] = fc | 0x80;
    r[1] = code;
    return 2;
  }

  bool reg(uint32_t a, uint16_t& out) {
    if (a >= 1 && a <= 3) {
      uint32_t up = ctx_.clock.ms() / 1000;
      out = a == 1 ? (uint16_t)ctx_.clock.timeState() : a == 2 ? (uint16_t)(up >> 16) : (uint16_t)up;
      return true;
    }
    for (auto* s : dev_.sensors())
      for (size_t i = 0; i < s->count(); i++) {
        const DValue& v = s->value(i);
        if (v.mbReg == 0 || a < v.mbReg || a > (uint32_t)v.mbReg + 2) continue;
        uint32_t u = (uint32_t)(int32_t)v.raw;
        uint8_t k = (uint8_t)(a - v.mbReg);
        out = k == 0 ? (uint16_t)(u >> 16) : k == 1 ? (uint16_t)u : v.state == DValueState::NotReady ? 0 : v.ok() ? 1 : 2;
        return true;
      }
    return false;
  }
};
