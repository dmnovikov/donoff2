// Base of a sensor read over Modbus RTU on its own UART: the question on one pass, the answer on a later one,
// loop() never waits. The link state goes to the log once per change.
#pragma once
#include <stdio.h>
#include "dplatform.h"
#include "../drtu.h"
#include "../dsensor.h"
#if defined(ESP8266)
#include <SoftwareSerial.h>
#endif

class DSensorRTU : public DSensor {
 public:
  struct Port {
    uint8_t rx, tx;
    uint32_t baud;
    uint8_t parity, stop;  // parity: 0 none, 1 even, 2 odd
  };

  DSensorRTU(DContext& ctx, const char* name, uint32_t periodMs, const Port& port, uint8_t addr, uint8_t fc,
             uint16_t start, uint16_t count)
      : DSensor(ctx, name, periodMs, 200), port_(port), addr_(addr), fc_(fc), start_(start), count_(count) {}

 protected:
  virtual void decode(const uint16_t* regs) = 0;  // fill the table from count registers

  void onBegin() override {
#if defined(ESP8266)
    static const EspSoftwareSerial::Config kCfg[2][3] = {{SWSERIAL_8N1, SWSERIAL_8E1, SWSERIAL_8O1},
                                                         {SWSERIAL_8N2, SWSERIAL_8E2, SWSERIAL_8O2}};
    // invert is given: the default constructor of SoftwareSerial leaves it unset, and begin() without it
    // takes that garbage (an object made with new got an inverted line).
    serial_.begin(port_.baud, kCfg[port_.stop == 2][port_.parity % 3], port_.rx, port_.tx, false);
#else
    static const uint32_t kCfg[2][3] = {{SERIAL_8N1, SERIAL_8E1, SERIAL_8O1}, {SERIAL_8N2, SERIAL_8E2, SERIAL_8O2}};
    serial_.begin(port_.baud, kCfg[port_.stop == 2][port_.parity % 3], port_.rx, port_.tx);
#endif
  }

  void request() override {
    while (serial_.available()) serial_.read();
    uint8_t q[8];
    drtu::makeRead(addr_, fc_, start_, count_, q);
    serial_.write(q, sizeof q);
  }

  bool collect() override {
    uint8_t b[64];
    size_t n = 0;
    while (serial_.available() && n < sizeof b) b[n++] = serial_.read();
    uint16_t regs[29];
    bool ok = count_ <= 29 && drtu::parseRead(b, n, addr_, fc_, count_, regs);
    report(ok, n);
    if (!ok) return false;
    decode(regs);
    return true;
  }

 private:
  void report(bool ok, size_t n) {
    if (ok == ok_ && told_) return;
    ok_ = ok;
    told_ = true;
    char t[96];
    static const char kPar[] = "NEO";
    char port[40];
    snprintf(port, sizeof port, "addr %u rx=%u tx=%u %lu 8%c%u", addr_, port_.rx, port_.tx, (unsigned long)port_.baud,
             kPar[port_.parity % 3], port_.stop);
    if (ok) snprintf(t, sizeof t, "N:%s ok, %s", name_, port);
    else snprintf(t, sizeof t, "E:%s no answer (%u bytes), %s", name_, (unsigned)n, port);
    ctx_.ev.log(t);
  }

  Port port_;
  uint8_t addr_, fc_;
  uint16_t start_, count_;
  bool ok_ = false, told_ = false;
#if defined(ESP8266)
  SoftwareSerial serial_;
#else
  HardwareSerial& serial_ = Serial2;
#endif
};
