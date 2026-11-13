// Which MQTT broker to use: the local one of the site (found on the LAN by mDNS) or the one of the
// settings (mqtt.lab240.ru). No hardware here: the MQTT output asks and reports, the finder searches.
//
//   before every connect: search when beforeConnect() says so, then searched(); connect to host():port()
//   after the connect:    connected(ok)
//   while connected:      recheckDue() -> search in the background -> searched() -> switchWanted()
//
// A local broker that answers the search but does not take the connection kLocalFails times in a row
// is skipped until the next recheck; the device goes to the broker of the settings meanwhile.
#pragma once
#include <stdint.h>
#include <string>

class DBrokerPick {
 public:
  static constexpr uint32_t kRecheckMs = 5 * 60 * 1000ul;
  static constexpr uint8_t kLocalFails = 3;

  // enabled = the setting mqttlocal; host and port = the broker of the settings.
  void configure(bool enabled, const std::string& host, uint16_t port) {
    enabled_ = enabled;
    host_ = host;
    port_ = port;
  }

  // Before a connect attempt: search first?
  bool beforeConnect(uint32_t now) {
    if (!enabled_) return false;
    if (localFails_ >= kLocalFails) {
      if (searchedOnce_ && now - lastSearch_ < kRecheckMs) return false;  // the local one keeps failing: wait
      localFails_ = 0;
    }
    return true;
  }

  // The result of a search: found = a broker with the donoff tag answered.
  void searched(bool found, const std::string& ip, uint16_t port, uint32_t now) {
    if (localFails_ >= kLocalFails && searchedOnce_ && now - lastSearch_ >= kRecheckMs) localFails_ = 0;  // a new chance
    searchedOnce_ = true;
    lastSearch_ = now;
    found_ = found && !ip.empty() && port;
    if (found_) {
      localIp_ = ip;
      localPort_ = port;
    }
  }

  bool useLocal() const { return enabled_ && found_ && localFails_ < kLocalFails; }
  const std::string& host() const { return useLocal() ? localIp_ : host_; }
  uint16_t port() const { return useLocal() ? localPort_ : port_; }

  // The connect attempt to host():port() is over.
  void connected(bool ok) {
    bool local = useLocal();
    if (local) localFails_ = ok ? 0 : localFails_ + 1;
    onLocal_ = ok && local;
    online_ = ok;
  }
  void lost() { online_ = onLocal_ = false; }

  // Connected to the broker of the settings: time to look for a local one again?
  bool recheckDue(uint32_t now) const { return enabled_ && online_ && !onLocal_ && now - lastSearch_ >= kRecheckMs; }
  // After a recheck: a local broker is there, drop the global one and connect to it.
  bool switchWanted() const { return online_ && !onLocal_ && useLocal(); }

  // For "sh mqtt" and the log: "local 192.168.30.52:1883", "global mqtt.lab240.ru:1883".
  std::string text() const {
    if (!online_) return "offline";
    return std::string(onLocal_ ? "local " : "global ") + (onLocal_ ? localIp_ : host_) + ":" + std::to_string(onLocal_ ? localPort_ : port_);
  }

 private:
  bool enabled_ = false, found_ = false, onLocal_ = false, online_ = false, searchedOnce_ = false;
  std::string host_, localIp_;
  uint16_t port_ = 1883, localPort_ = 0;
  uint8_t localFails_ = 0;
  uint32_t lastSearch_ = 0;
};
