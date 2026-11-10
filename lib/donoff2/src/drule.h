// DRule: one reason for a relay to move.
//   DBase -> DRule -> DRuleTemp -> DRuleHotter, DRuleCooler
//                  -> DRuleSchedule, DRuleAutoStop, DRuleLimiter
#pragma once
#include <stdlib.h>
#include "drelay.h"
#include "dsettings.h"
#include "dvalue.h"

namespace dscheme {
enum class Hour : uint8_t { Off, On, Unknown };

static const uint32_t kPresets[20] = {
    0b000000000000000000000000, 0b111111111111111111111111, 0b000000000000000000001111,
    0b111111000000000000111111, 0b000000011111111111111110, 0b000001111111111111111110,
    0b000000111100000000111110, 0b000011111000000000111110, 0b010101010101010101010101,
    0b101010101010101010101010, 0b111111111111000000001111, 0b001001001001001001001001,
    0b000100010001000100010001, 0b000010000100001000010001, 0b000000010000000100000001,
    0b000000000010000000000001, 0b000000000000000000000010, 0b111111111110000000000011,
    0b001111111111111000000000, 0b000000111100000000111111};

inline bool valid(int32_t n) { return (n >= 0 && n <= 20) || n == 99 || n == 100; }

// Schemes 1 (never) and 2 (always) need no clock, all the others do.
inline Hour at(const DContext& c, int32_t n) {
  if (n == 1) return Hour::Off;
  if (n == 2) return Hour::On;
  if (!c.clock.hasTime()) return Hour::Unknown;
  uint32_t bits;
  if (n == 99) bits = c.s.cschm1;
  else if (n == 100) bits = c.s.cschm2;
  else if (n >= 3 && n <= 20) bits = kPresets[n - 1];
  else return Hour::Unknown;
  return ((bits >> (23 - c.clock.hour())) & 1) ? Hour::On : Hour::Off;
}
}  // namespace dscheme

class DRule : public DBase {
 public:
  DRule(DContext& ctx, const char* name, DRelay& relay) : DBase(ctx, name), relay_(relay) {}
  virtual void loop() = 0;

 protected:
  DRelay& relay_;
};

// Keeps a value around a level. What differs between heating and cooling is the direction.
class DRuleTemp : public DRule {
 public:
  DRuleTemp(DContext& ctx, const char* name, DRelay& relay, const DValue* temp) : DRule(ctx, name, relay), temp_(temp) {}

  void loop() override {
    if (!active()) return;
    if (ctx_.s.lschm1 > 0) {  // a schedule and a thermostat do not share a relay
      char b[48];
      snprintf(b, sizeof b, "E:ignore %s, lschm,aofs", name_);
      ctx_.ev.info(b);
      clearMode();
      return;
    }
    if (temp_ == nullptr || !temp_->ok()) {
      relay_.off(sensorErrorText());
      return;
    }
    long level = level100(), delta = delta100();
    if (needOn(temp_->raw, level)) relay_.on(name_);
    else if (needOff(temp_->raw, level, delta)) relay_.off(name_);
  }

  // the units of old settings: < 100 are degrees, otherwise 1/100
  static long toLevel100(int32_t v) { return labs(v) < 100 ? v * 100L : v; }
  // < 50 are tenths, otherwise 1/100
  static long toDelta100(int32_t v) { return v < 50 ? v * 10L : v; }

 protected:
  virtual bool active() const = 0;
  virtual void clearMode() = 0;
  virtual const char* sensorErrorText() const = 0;
  virtual bool needOn(long t, long level) const = 0;
  virtual bool needOff(long t, long level, long delta) const = 0;
  virtual long level100() const { return dayNightLevel(); }

  long dayNightLevel() const {
    if (!ctx_.clock.hasTime()) return toLevel100(ctx_.s.ctempl);
    switch (dscheme::at(ctx_, ctx_.s.onoffschm)) {
      case dscheme::Hour::On: return toLevel100(ctx_.s.dtempl);
      case dscheme::Hour::Off: return toLevel100(ctx_.s.ntempl);
      default: return toLevel100(ctx_.s.ctempl);
    }
  }
  long delta100() const { return toDelta100(ctx_.s.tdelta); }

  const DValue* temp_;
};

class DRuleHotter : public DRuleTemp {
 public:
  DRuleHotter(DContext& ctx, DRelay& relay, const DValue* temp) : DRuleTemp(ctx, "hotter", relay, temp) {}

 protected:
  bool active() const override { return ctx_.s.hotter == 1 || ctx_.s.hotter == 2; }
  void clearMode() override { ctx_.s.hotter = 0; }
  const char* sensorErrorText() const override { return "hotter, sensor not ready"; }
  bool needOn(long t, long level) const override { return t < level; }
  bool needOff(long t, long level, long delta) const override { return t >= level + delta; }
  long level100() const override {
    if (ctx_.s.hotter != 2) return dayNightLevel();
    if (!ctx_.clock.hasTime()) return toLevel100(ctx_.s.ctempl);
    return toLevel100(ctx_.s.tmatrix[ctx_.clock.hour()]);
  }
};

class DRuleCooler : public DRuleTemp {
 public:
  DRuleCooler(DContext& ctx, DRelay& relay, const DValue* temp) : DRuleTemp(ctx, "cooler", relay, temp) {}

 protected:
  bool active() const override { return ctx_.s.cooler != 0; }
  void clearMode() override { ctx_.s.cooler = 0; }
  const char* sensorErrorText() const override { return "cooler, sensor err"; }
  bool needOn(long t, long level) const override { return t > level; }
  bool needOff(long t, long level, long delta) const override { return t <= level - delta; }
};

// A relay by the clock. With aofs it goes on once at the start of every "on" hour.
class DRuleSchedule : public DRule {
 public:
  DRuleSchedule(DContext& ctx, DRelay& relay, const int32_t* scheme, const int32_t* aofs, const int32_t* noTime, bool yieldsToThermostat)
      : DRule(ctx, "lschm", relay), scheme_(scheme), aofs_(aofs), noTime_(noTime), yields_(yieldsToThermostat), hadTime_(ctx.clock.hasTime()) {}

  void loop() override {
    bool hadTime = hadTime_;
    hadTime_ = ctx_.clock.hasTime();
    if (*scheme_ == 0) return;
    if (yields_ && (ctx_.s.hotter != 0 || ctx_.s.cooler != 0)) return;
    uint8_t h = ctx_.clock.hour();
    if (!hadTime && hadTime_ && *scheme_ == 2) relay_.setLastHourOn(h);  // the clock jumped: not a new hour

    dscheme::Hour bit = dscheme::at(ctx_, *scheme_);
    if (bit == dscheme::Hour::Unknown) {
      if (*noTime_ == 0) {
        relay_.off("lschm, no time sync");
        return;
      }
      bit = dscheme::Hour::On;
    }
    if (bit == dscheme::Hour::Off) {
      relay_.off("lschm,off");
      relay_.resetLastHourOn();
      return;
    }
    if (relay_.isOn()) {
      relay_.setLastHourOn(h);
      return;
    }
    if (relay_.lastHourOn() != h || *aofs_ == 0) {
      relay_.setLastHourOn(h);
      relay_.on("lschm,on");
    }
  }

 private:
  const int32_t* scheme_;
  const int32_t* aofs_;
  const int32_t* noTime_;
  bool yields_;
  bool hadTime_;
};

// aofs: any "on" of the relay ends after so many seconds.
class DRuleAutoStop : public DRule {
 public:
  DRuleAutoStop(DContext& ctx, DRelay& relay, const int32_t* sec, bool yieldsToThermostat)
      : DRule(ctx, "aofs", relay), sec_(sec), yields_(yieldsToThermostat) {}

  void loop() override {
    if (*sec_ <= 0 || !relay_.isOn()) return;
    if (yields_ && (ctx_.s.hotter != 0 || ctx_.s.cooler != 0)) return;
    if (ctx_.clock.ms() - relay_.startMs() > (uint32_t)*sec_ * 1000UL) relay_.off("aofs,off");
  }

 private:
  const int32_t* sec_;
  bool yields_;
};

// A load above its limit for too long is switched off, and it stays off over a reboot.
class DRuleLimiter : public DRule {
 public:
  DRuleLimiter(DContext& ctx, DRelay& relay, const DValue* overSec, const int32_t* limitSec, int32_t* scheme)
      : DRule(ctx, "limiter", relay), over_(overSec), limit_(limitSec), scheme_(scheme) {}

  void loop() override {
    if (!over_->ok()) return;
    if (over_->raw == 0) {
      tripped_ = false;
      return;
    }
    if (tripped_ || over_->raw <= *limit_) return;
    tripped_ = true;
    *scheme_ = 1;  // always off, until somebody sets the schedule again
    ctx_.saveWanted = true;
    relay_.off("strong_curr");
    ctx_.ev.info("strong_curr: off, current strong time");
  }

 private:
  const DValue* over_;
  const int32_t* limit_;
  int32_t* scheme_;
  bool tripped_ = false;
};
