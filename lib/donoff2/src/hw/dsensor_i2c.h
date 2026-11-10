// Base of every I2C sensor: the bus, the address (one or two to try), register reads and writes,
// and the bus state in the log once per change. A sensor lost on the bus is found and set up again.
#pragma once
#include <Wire.h>
#include <stdio.h>
#include "../dsensor.h"

class DSensorI2C : public DSensor {
 public:
  // Error codes in the log: 1..5 from Wire (2 = no answer at the address, 5 = the bus is held low),
  // kWrongChip = something else answers at the address, kReadFailed = a reading did not come back.
  static constexpr uint8_t kWrongChip = 8, kReadFailed = 9;

  DSensorI2C(DContext& ctx, const char* name, uint32_t periodMs, uint32_t requestMs, uint32_t startDelayMs,
             uint8_t sda, uint8_t scl, uint8_t addr, uint8_t addr2 = 0)
      : DSensor(ctx, name, periodMs, requestMs, startDelayMs), sda_(sda), scl_(scl), addr_(addr) {
    addrs_[0] = addr;
    addrs_[1] = addr2;
  }

 protected:
  virtual uint8_t setupChip() { return 0; }  // the chip answered at addr_: check and set it up, 0 = ok
  virtual bool measure() = 0;                // fill the table; false = the reading failed

  bool ready() const { return ready_; }
  uint8_t addr() const { return addr_; }

  void onBegin() override { Wire.begin(sda_, scl_); }

  bool collect() final {
    if (!ready_) {
      uint8_t err = find();
      if (err == 0) err = setupChip();
      report(err);
      if (err) return false;
      ready_ = true;
      for (size_t i = 0; i < count(); i++) value(i).notReady();  // the first measurement is not done yet
      return true;
    }
    if (measure()) return true;
    ready_ = false;
    report(kReadFailed);
    return false;
  }

  // Bus helpers: 0 = ok, otherwise a Wire error code.
  uint8_t command(uint8_t c) {
    Wire.beginTransmission(addr_);
    Wire.write(c);
    return Wire.endTransmission();
  }
  uint8_t writeReg(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission();
  }
  uint8_t writeReg16(uint8_t reg, uint16_t v) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    Wire.write((uint8_t)(v >> 8));
    Wire.write((uint8_t)v);
    return Wire.endTransmission();
  }
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    return readRaw(buf, n);
  }
  bool readRaw(uint8_t* buf, uint8_t n) {
    if (Wire.requestFrom((int)addr_, (int)n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
  }

 private:
  uint8_t find() {
    uint8_t err = 2;
    for (uint8_t a : addrs_) {
      if (!a) continue;
      Wire.beginTransmission(a);
      err = Wire.endTransmission();
      if (err == 0) {
        addr_ = a;
        return 0;
      }
    }
    return err;
  }

  void report(uint8_t err) {
    if (err == lastErr_) return;
    lastErr_ = err;
    char b[64];
    if (err == 0) snprintf(b, sizeof b, "N:%s ok, addr 0x%02X sda=%u scl=%u", name_, addr_, sda_, scl_);
    else snprintf(b, sizeof b, "E:%s i2c error %u, sda=%u scl=%u", name_, err, sda_, scl_);
    ctx_.ev.log(b);
  }

  uint8_t sda_, scl_, addr_;
  uint8_t addrs_[2];
  uint8_t lastErr_ = 255;
  bool ready_ = false;
};
