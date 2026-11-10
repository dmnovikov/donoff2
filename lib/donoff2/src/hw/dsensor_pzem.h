// PZEM-004T v3: the question now, the answer on a later pass, loop() never waits for it.
#pragma once
#include "dplatform.h"
#include "../dpzem.h"
#include "../dsensor.h"
#if defined(ESP8266)
#include <SoftwareSerial.h>
#endif

class DSensorPZEM : public DSensor {
 public:
  enum { Current, Power, Voltage, Energy };

  DSensorPZEM(DContext& ctx, uint8_t rx, uint8_t tx, uint16_t mbReg)
      : DSensor(ctx, "pzem", 5000, 200), rx_(rx), tx_(tx) {
#if defined(ESP8266)
    serial_.begin(9600, SWSERIAL_8N1, rx, tx);
#endif
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
  void onBegin() override {
#if !defined(ESP8266)
    serial_.begin(9600, SERIAL_8N1, rx_, tx_);
#endif
  }

  void request() override {
    while (serial_.available()) serial_.read();
    uint8_t q[8];
    dpzem::makeRequest(q);
    serial_.write(q, sizeof q);
  }

  bool collect() override {
    uint8_t b[dpzem::kAnswer];
    size_t n = 0;
    while (serial_.available() && n < sizeof b) b[n++] = serial_.read();
    dpzem::Reading r;
    if (!dpzem::parse(b, n, r)) return false;
    value(Current).set(r.current);
    value(Power).set(r.power);
    value(Voltage).set(r.voltage);
    value(Energy).set(r.energy);
    return true;
  }

 private:
  void add(const char* n, const char* unit, uint16_t reg, uint8_t type, const char* label) {
    value(addValue(n, 100, unit, reg, type)).label = label;
  }
  uint8_t rx_, tx_;
#if defined(ESP8266)
  SoftwareSerial serial_;
#else
  HardwareSerial& serial_ = Serial2;
#endif
};
