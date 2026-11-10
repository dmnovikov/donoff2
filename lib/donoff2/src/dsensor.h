// DSensor: one class per piece of hardware; its values are lines of one table.
//   DBase -> DSensor -> DSensorDS18B20, DSensorPZEM ...
//                    -> DSensorVirtual -> DSensorOverTime
#pragma once
#include <string.h>
#include <vector>
#include "dbase.h"
#include "dsettings.h"
#include "dvalue.h"

class DSensor : public DBase {
 public:
  DSensor(DContext& ctx, const char* name, uint32_t periodMs, uint32_t requestMs = 0, uint32_t startDelayMs = 0)
      : DBase(ctx, name), period_(periodMs), requestMs_(requestMs), startDelay_(startDelayMs) {
    values_.reserve(4);
  }

  void begin() {
    born_ = ctx_.clock.ms();
    onBegin();
  }

  // Never waits: a sensor that needs time is asked now and read on a later pass.
  void loop() {
    uint32_t now = ctx_.clock.ms();
    if (now - born_ < startDelay_) {
      if (now - lastWarm_ >= period_) {
        lastWarm_ = now;
        warmup();
      }
      return;
    }
    if (waiting_) {
      if (now - requestedAt_ < requestMs_) return;
      waiting_ = false;
      read(now);
      return;
    }
    if (!first_ && now - last_ < period_) return;
    first_ = false;
    if (requestMs_ == 0) {
      read(now);
      return;
    }
    request();
    requestedAt_ = now;
    waiting_ = true;
  }

  size_t count() const { return values_.size(); }
  DValue& value(size_t i) { return values_[i]; }
  // Several values also go as one JSON to /out/sensors/<group>.
  const char* group() const { return group_; }
  void setGroup(const char* g) { group_ = g; }
  void setMedian(bool on) {
    for (auto& v : values_) v.median = on;
  }
  DValue* find(const char* n) {
    for (auto& v : values_)
      if (strcmp(v.name, n) == 0) return &v;
    return nullptr;
  }

 protected:
  size_t addValue(const char* n, long mult, const char* unit, uint16_t mbReg = 0, uint8_t sType = 0) {
    values_.emplace_back(n, mult, unit, mbReg, sType);
    return values_.size() - 1;
  }
  void restartDelay() { born_ = ctx_.clock.ms(); }  // the hardware just appeared: wait again
  virtual void warmup() {}      // during the start delay: lets a sensor settle
  virtual void onBegin() {}
  virtual void request() {}     // start a measurement (when requestMs is not 0)
  virtual bool collect() = 0;   // fill the whole table; false = the sensor did not answer

 private:
  void read(uint32_t now) {
    last_ = now;
    if (collect()) return;
    for (auto& v : values_) v.fail();
  }

  std::vector<DValue> values_;
  uint32_t period_, requestMs_, startDelay_;
  uint32_t born_ = 0, last_ = 0, requestedAt_ = 0, lastWarm_ = 0;
  const char* group_ = nullptr;
  bool first_ = true, waiting_ = false;
};

// A sensor without hardware: counts its values from other values.
class DSensorVirtual : public DSensor {
 public:
  DSensorVirtual(DContext& ctx, const char* name, uint32_t periodMs) : DSensor(ctx, name, periodMs) {}
};

// temp_odd: the difference of two values.
class DSensorDifference : public DSensorVirtual {
 public:
  DSensorDifference(DContext& ctx, const char* name, const DValue* a, const DValue* b, uint16_t mbReg = 0)
      : DSensorVirtual(ctx, name, 1000), a_(a), b_(b) {
    addValue(name, a->mult, a->unit, mbReg, 8);
  }

 protected:
  bool collect() override {
    if (!a_->ok() || !b_->ok()) return false;
    value(0).setRaw(b_->raw - a_->raw);
    return true;
  }

 private:
  const DValue* a_;
  const DValue* b_;
};

// strong_sec: for how long a value has been above its threshold (<= 0 = not watching).
class DSensorOverTime : public DSensorVirtual {
 public:
  DSensorOverTime(DContext& ctx, const char* name, const DValue* src, const int32_t* threshold, uint16_t mbReg = 0)
      : DSensorVirtual(ctx, name, 1000), src_(src), threshold_(threshold) {
    addValue(name, 1, "s", mbReg);
  }

 protected:
  bool collect() override {
    DValue& out = value(0);
    if (*threshold_ <= 0) {
      started_ = false;
      out.setRaw(0);
      return true;
    }
    if (!src_->ok()) {  // no opinion: the count stays
      if (out.state == DValueState::NotReady) out.setRaw(0);
      return true;
    }
    uint32_t now = ctx_.clock.ms();
    if (src_->raw <= *threshold_) {
      started_ = false;
      out.setRaw(0);
    } else {
      if (!started_) {
        started_ = true;
        since_ = now;
      }
      out.setRaw((long)((now - since_) / 1000));
    }
    return true;
  }

 private:
  const DValue* src_;
  const int32_t* threshold_;
  bool started_ = false;
  uint32_t since_ = 0;
};
