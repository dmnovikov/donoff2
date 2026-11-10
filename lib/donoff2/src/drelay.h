// DRelay: what a relay does; the concrete class only knows how to move the pin.
#pragma once
#include <stdio.h>
#include "dbase.h"

class DRelay : public DBase {
 public:
  DRelay(DContext& ctx, const char* name) : DBase(ctx, name) {}

  bool isOn() const { return on_; }
  uint32_t startMs() const { return startMs_; }
  uint32_t stopMs() const { return stopMs_; }

  // true = the state changed; every change goes to the log as "L:r1:ON, reason"
  bool on(const char* reason) { return change(true, reason); }
  bool off(const char* reason) { return change(false, reason); }

  // the hour the schedule last turned the relay on: aofs waters once per hour
  uint8_t lastHourOn() const { return lastHourOn_; }
  void setLastHourOn(uint8_t h) { lastHourOn_ = h; }
  void resetLastHourOn() { lastHourOn_ = kNoHour; }
  static constexpr uint8_t kNoHour = 24;

 protected:
  virtual void write(bool on) = 0;

 private:
  bool change(bool want, const char* reason) {
    if (on_ == want) return false;
    write(want);
    on_ = want;
    if (want) startMs_ = ctx_.clock.ms();
    else stopMs_ = ctx_.clock.ms();
    char buf[64];
    snprintf(buf, sizeof buf, "L:%s:%s%s%s", name_, want ? "ON" : "OFF", reason[0] ? ", " : "", reason);
    ctx_.ev.log(buf);
    return true;
  }

  bool on_ = false;
  uint32_t startMs_ = 0, stopMs_ = 0;
  uint8_t lastHourOn_ = kNoHour;
};
