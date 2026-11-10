// The setup portal (WiFiManager). It does not block: thermostat and schedules keep working behind it.
// The fields come from the parameter table marked "portal", the RTU ones only on boards with RS-485.
#pragma once
#include <WiFiManager.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "dplatform.h"
#include "../ddevice.h"

#ifndef DONOFF_AP_PASSWORD
#define DONOFF_AP_PASSWORD "donoff2setup"
#endif

class DPortalWM : public DBase {
 public:
  DPortalWM(DContext& ctx, DDevice& dev) : DBase(ctx, "portal"), dev_(dev) {}

  bool active() const { return active_; }

  void start() {
    if (active_) return;
    DSettings& s = ctx_.s;
    addText("mqtt_server", "mqtt server", s.mqttServer, sizeof s.mqttServer);
    addText("mqtt_user", "mqtt user", s.mqttUser, sizeof s.mqttUser);
    addText("mqtt_pass", "mqtt password (empty: keep)", s.mqttPass, sizeof s.mqttPass, false);
    addText("mqtt_port", "mqtt port", s.mqttPort, sizeof s.mqttPort);
    addText("dev_id", "device id", s.devId, sizeof s.devId);
    addText("email", "email", s.email, sizeof s.email);
    addText("dev_type", "device type (empty: of the board)", s.devType, sizeof s.devType);
    for (auto& p : dev_.commands().params()) {
      bool rtu = strncmp(p.name, "rtu", 3) == 0;
      if (!p.portal && !(rtu && dev_.hasRs485())) continue;
      numbers_.push_back({new WiFiManagerParameter(p.name, p.name, std::to_string(*p.v).c_str(), 7), &p});
      wm_.addParameter(numbers_.back().field);
    }
    wm_.setConfigPortalBlocking(false);
    wm_.setConfigPortalTimeout(kTimeoutSec);
    wm_.setBreakAfterConfig(true);
    wm_.setConnectTimeout(20);
    wm_.setDarkMode(true);
    wm_.setSaveConfigCallback([this]() { saved_ = true; });
    wm_.startConfigPortal("donoff-ap", DONOFF_AP_PASSWORD);
    active_ = true;
    ctx_.ev.log("N:portal on");
  }

  void loop() {
    if (!active_) return;
    wm_.process();
    if (saved_) {
      apply();
      dev_.saveNow();
      ctx_.ev.log("N:portal saved");
      ctx_.rebootWanted = true;
      active_ = false;
      return;
    }
    if (wm_.getConfigPortalActive()) return;
    active_ = false;  // time is out: back to the network, nothing changed
    ctx_.ev.log("N:portal off");
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }

 private:
  static constexpr unsigned long kTimeoutSec = 300;

  struct Text {
    WiFiManagerParameter* field;
    char* to;
    size_t size;
    bool show;
  };
  struct Number {
    WiFiManagerParameter* field;
    DCommands::Param* param;
  };

  void addText(const char* id, const char* label, char* to, size_t size, bool show = true) {
    texts_.push_back({new WiFiManagerParameter(id, label, show ? to : "", (int)size - 1), to, size, show});
    wm_.addParameter(texts_.back().field);
  }

  void apply() {
    for (auto& t : texts_) {
      const char* v = t.field->getValue();
      if (v[0] == 0 && !t.show) continue;
      strlcpy(t.to, v, t.size);
    }
    for (auto& n : numbers_) {
      char* end;
      long v = strtol(n.field->getValue(), &end, 10);
      if (end != n.field->getValue() && *end == 0 && v >= n.param->lo && v <= n.param->hi) *n.param->v = v;
    }
  }

  DDevice& dev_;
  WiFiManager wm_;
  std::vector<Text> texts_;
  std::vector<Number> numbers_;
  bool active_ = false;
  volatile bool saved_ = false;
};
