#pragma once
#include <Arduino.h>
#if defined(ESP8266)
#include <coredecls.h>
#else
#include <esp_sntp.h>
#endif
#include <time.h>
#include "../dbase.h"
#include "../dsettings.h"

class DClockNtp : public DClock {
 public:
  void begin(int32_t tz) {
    tz_ = tz;
#if defined(ESP8266)
    settimeofday_cb([this]() { synced(); });
#else
    self_ = this;
    sntp_set_time_sync_notification_cb([](struct timeval*) { self_->synced(); });
#endif
    apply();
  }
  void setTz(int32_t tz) {
    if (tz == tz_) return;
    tz_ = tz;
    apply();
  }
  uint32_t ms() const override { return millis(); }
  DTimeState timeState() const override {
    if (!synced_) return DTimeState::None;
    return millis() - lastSync_ > kStaleMs ? DTimeState::Stale : DTimeState::Synced;
  }
  uint8_t hour() const override { return now()->tm_hour; }
  uint8_t minute() const override { return now()->tm_min; }
  int year() const override { return now()->tm_year + 1900; }

 private:
  static constexpr uint32_t kStaleMs = 3UL * 3600UL * 1000UL;
  static struct tm* now() {
    time_t t = time(nullptr);
    return localtime(&t);
  }
  void synced() {
    lastSync_ = millis();
    synced_ = true;
  }
#if !defined(ESP8266)
  static inline DClockNtp* self_ = nullptr;
#endif
  void apply() { configTime(tz_ * 3600L, 0, "pool.ntp.org", "time.nist.gov"); }
  int32_t tz_ = 0;
  volatile uint32_t lastSync_ = 0;
  volatile bool synced_ = false;
};
