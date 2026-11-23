// temp_remote: a temperature of another device, taken from its MQTT topic (setting rsensor=<dev>/<value>,
// after reset). The value is lost (the thermostat turns off) when nothing came for rtimeout seconds,
// when the source says offline (out/status, its Last Will) and when it sends an error (-127, -128).
#pragma once
#include <stdlib.h>
#include <string.h>
#include <string>
#include "dsensor.h"

class DSensorRemote : public DSensorVirtual {
 public:
  static constexpr uint16_t kModbus = 168;

  DSensorRemote(DContext& ctx, const char* source) : DSensorVirtual(ctx, "temp_remote", 1000), source_(source) {
    addValue("temp_remote", 100, "C", kModbus, 1);
    size_t slash = source_.find('/');
    dev_ = source_.substr(0, slash);
    value_ = source_.substr(slash + 1);
  }

  // <dev>/<value>: letters, digits, '_' and '-' on both sides
  static bool validSource(const std::string& s, size_t max) {
    size_t slash = s.find('/');
    if (s.size() >= max || slash == std::string::npos || slash == 0 || slash + 1 == s.size()) return false;
    for (size_t i = 0; i < s.size(); i++) {
      char c = s[i];
      bool okc = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      if (i != slash && !okc) return false;
    }
    return true;
  }

  const std::string& source() const { return source_; }
  const std::string& sourceDev() const { return dev_; }
  const std::string& sourceValue() const { return value_; }

  // a message of <prefix>/<dev>/out/sensors/<value>: "21.50"
  void feedValue(const char* p, size_t n) {
    std::string t(p, n);
    char* end;
    double v = strtod(t.c_str(), &end);
    if (t.empty() || *end != 0 || v <= -100 || v > 2000) {  // -127 no sensor, -128 not ready, or not a number
      bad_ = true;
      got_ = ctx_.clock.ms();
      have_ = true;
      return;
    }
    bad_ = false;
    v_ = (float)v;
    got_ = ctx_.clock.ms();
    have_ = true;
  }
  // a message of <prefix>/<dev>/out/status: "online" / "offline"
  void feedStatus(const char* p, size_t n) { offline_ = std::string(p, n) == "offline"; }

  // seconds since the last message, -1: none yet
  long age() const { return have_ ? (long)((ctx_.clock.ms() - got_) / 1000) : -1; }

 protected:
  bool collect() override {
    Problem p = Problem::None;
    if (!have_) p = Problem::Waiting;
    else if (offline_) p = Problem::Offline;
    else if (ctx_.clock.ms() - got_ > (uint32_t)ctx_.s.rtimeout * 1000) p = Problem::Stale;
    else if (bad_) p = Problem::SourceError;
    report(p);
    if (p != Problem::None) return false;
    value(0).set(v_);
    return true;
  }

 private:
  enum class Problem : uint8_t { None, Waiting, Offline, Stale, SourceError };

  void report(Problem p) {  // once per change
    if (p == problem_) return;
    problem_ = p;
    if (p == Problem::Waiting) return;
    static const char* const kText[] = {"N:temp_remote ok from ", "", "E:temp_remote source offline ",
                                        "E:temp_remote no data ", "E:temp_remote source error "};
    ctx_.ev.log((std::string(kText[(uint8_t)p]) + source_).c_str());
  }

  std::string source_, dev_, value_;
  float v_ = 0;
  uint32_t got_ = 0;
  bool have_ = false, bad_ = false, offline_ = false;
  Problem problem_ = Problem::Waiting;
};
