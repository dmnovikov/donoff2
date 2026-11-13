// Looks for the local broker of the site on the LAN: mDNS service _mqtt._tcp with the TXT record
// donoff=1 (avahi on the NAPI announces it). The search does not block: start(), then poll done().
// The device also answers mDNS as <devId>.local while it is on.
#pragma once
#include "dplatform.h"
#include <string>
#if defined(ESP8266)
#include <ESP8266mDNS.h>
#endif

class DBrokerFinder {
 public:
  static constexpr uint32_t kWaitMs = 1500;  // answers come in 0.1-0.2 s on the bench

#if defined(ESP8266)
  // Call while Wi-Fi is up; the responder starts once.
  void loop(const char* name) {
    if (!started_) started_ = MDNS.begin(name);
    if (started_) MDNS.update();
  }

  bool start() {
    stop();
    if (!started_) return false;
    got_ = false;
    t0_ = millis();
    q_ = MDNS.installServiceQuery("mqtt", "tcp", [this](const MDNSResponder::MDNSServiceInfo&, MDNSResponder::AnswerType, bool set) {
      if (set) got_ = true;
    });
    return q_ != nullptr;
  }

  bool busy() const { return q_ != nullptr; }

  // The search is over: an answer with the tag and an address, or the time is up.
  bool done(std::string& ip, uint16_t& port, bool& found) {
    if (!q_) return false;
    found = pick(ip, port);
    if (!found && millis() - t0_ < kWaitMs) return false;
    stop();
    return true;
  }

  void stop() {
    if (q_) MDNS.removeServiceQuery(q_);
    q_ = nullptr;
  }

 private:
  bool pick(std::string& ip, uint16_t& port) {
    if (!got_) return false;
    for (auto& i : MDNS.answerInfo(q_)) {
      if (!i.txtAvailable() || !i.hostPortAvailable()) continue;
      const char* tag = i.value("donoff");
      auto ips = i.IP4Adresses();
      if (!tag || strcmp(tag, "1") != 0 || ips.empty()) continue;
      ip = ips[0].toString().c_str();
      port = i.hostPort();
      return true;
    }
    return false;
  }

  MDNSResponder::hMDNSServiceQuery q_ = nullptr;
  uint32_t t0_ = 0;
  bool started_ = false, got_ = false;
#else
  // ESP32: not done yet, the broker of the settings is used.
  void loop(const char*) {}
  bool start() { return false; }
  bool busy() const { return false; }
  bool done(std::string&, uint16_t&, bool&) { return false; }
  void stop() {}
#endif
};
