// The root of the donoff2 classes: everything a class needs from the device comes through DContext.
#pragma once
#include <string>
#include <stdint.h>

struct DSettings;

enum class DTimeState : uint8_t { None, Synced, Stale };  // Stale: NTP is lost, the internal clock keeps going

class DClock {
 public:
  virtual ~DClock() = default;
  virtual uint32_t ms() const = 0;
  virtual DTimeState timeState() const = 0;
  virtual uint8_t hour() const = 0;
  virtual uint8_t minute() const = 0;
  virtual int year() const = 0;
  bool hasTime() const { return timeState() != DTimeState::None; }
};

// Where the device speaks: /out/info and /out/log over MQTT, the serial port on the bench.
class DEvents {
 public:
  virtual ~DEvents() = default;
  virtual void info(const char* text) = 0;
  virtual void log(const char* text) = 0;
};

// Several listeners at once: the serial port and MQTT.
class DEventsHub : public DEvents {
 public:
  void add(DEvents* e) {
    if (n_ < kMax) sinks_[n_++] = e;
  }
  void info(const char* t) override {
    for (uint8_t i = 0; i < n_; i++) sinks_[i]->info(t);
  }
  void log(const char* t) override {
    for (uint8_t i = 0; i < n_; i++) sinks_[i]->log(t);
  }

 private:
  static constexpr uint8_t kMax = 3;
  DEvents* sinks_[kMax] = {};
  uint8_t n_ = 0;
};

struct DContext {
  DClock& clock;
  DSettings& s;
  DEvents& ev;
  bool saveWanted = false;
  bool rebootWanted = false;
  std::string broker = "offline";  // where MQTT goes: "local ip:port", "global host:port", "offline"
};

class DBase {
 public:
  DBase(DContext& ctx, const char* name) : ctx_(ctx), name_(name) {}
  virtual ~DBase() = default;
  const char* name() const { return name_; }

 protected:
  DContext& ctx_;
  const char* name_;
};
