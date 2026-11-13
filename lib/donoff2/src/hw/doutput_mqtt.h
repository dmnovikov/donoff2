// The topics of donoff-lib, plus two new ones: out/meta (what the device is) and out/status (Last Will).
// The broker: the local one of the site when mDNS finds it (mqttlocal=1), otherwise mqttServer; see dbroker.h.
#pragma once
#include "dplatform.h"
#include <PubSubClient.h>
#include <deque>
#include <string>
#include <time.h>
#include <vector>
#include "../dbroker.h"
#include "../doutput.h"
#include "dbroker_mdns.h"

class DOutputMQTT : public DOutput, public DEvents {
 public:
  DOutputMQTT(DContext& ctx, DDevice& dev, const char* fw) : DOutput(ctx, "mqtt", dev), fw_(fw), mqtt_(net_) {}

  void begin() override {
    DSettings& s = ctx_.s;
    base_ = std::string("/") + s.mqttUser + "/" + s.devId + "/";
    pick_.configure(s.mqttlocal != 0, s.mqttServer, atoi(s.mqttPort));
    mqtt_.setBufferSize(1024);
    mqtt_.setSocketTimeout(3);
    mqtt_.setCallback([this](char*, uint8_t* p, unsigned int n) {
      if (inbox_.size() < kInbox) inbox_.emplace_back((const char*)p, n);
    });
  }

  void loop() override {
    uint32_t now = millis();
    bool online = WiFi.status() == WL_CONNECTED;
    if (online) finder_.loop(ctx_.s.devId);
    if (!online || !mqtt_.connected()) {
      if (up_) {
        up_ = false;
        pick_.lost();
        ctx_.broker = pick_.text();
      }
      std::string ip;
      uint16_t port = 0;
      bool found = false;
      if (finder_.busy()) {  // the search before this attempt
        if (!online) finder_.stop();
        else if (!finder_.done(ip, port, found)) return;
        pick_.searched(found, ip, port, now);
        attempt(online);
        return;
      }
      if (tried_ && now - lastTry_ < kRetryMs) return;
      tried_ = true;
      lastTry_ = now;
      if (online && pick_.beforeConnect(now)) {
        if (finder_.start()) return;  // connect when the search is over
        pick_.searched(false, ip, port, now);
      }
      attempt(online);
      return;
    }
    fails_ = 0;
    mqtt_.loop();
    recheck(now);
    while (!inbox_.empty()) {
      std::string cmd = inbox_.front();
      inbox_.pop_front();
      dev_.commands().handle(cmd.c_str());
    }
    publishRelayChanges();
    if (published_ && now - lastPub_ < kPublishMs) return;
    published_ = true;
    lastPub_ = now;
    publishAll();
  }

  bool connected() { return mqtt_.connected(); }
  void info(const char* t) override { pub("out/info", t); }
  void log(const char* t) override { pub("out/log", t); }

 private:
  // The old firmware rebooted after 100 attempts 30 s apart: the same 50 minutes.
  static constexpr uint32_t kRetryMs = 10000, kPublishMs = 8000, kFailsToReboot = 300;
  static constexpr size_t kInbox = 4;

  void pub(const std::string& sub, const std::string& v, bool retain = false) {
    if (mqtt_.connected()) mqtt_.publish((base_ + sub).c_str(), v.c_str(), retain);
  }

  void attempt(bool online) {
    if (online) connect();
    bool ok = online && mqtt_.connected();
    pick_.connected(ok);
    fails_ = ok ? 0 : fails_ + 1;
    if (fails_ > kFailsToReboot && ctx_.s.autoreboot) ctx_.rebootWanted = true;
    if (!ok) return;
    up_ = true;
    ctx_.broker = pick_.text();
    ctx_.ev.log(("N:mqtt " + ctx_.broker).c_str());
  }

  // On the broker of the settings: now and then look for the local one, and move there when it is back.
  void recheck(uint32_t now) {
    std::string ip;
    uint16_t port = 0;
    bool found = false;
    if (finder_.busy()) {
      if (!finder_.done(ip, port, found)) return;
      pick_.searched(found, ip, port, now);
      if (!pick_.switchWanted()) return;
      mqtt_.disconnect();
      up_ = false;
      pick_.lost();
      attempt(true);
      return;
    }
    if (pick_.recheckDue(now) && !finder_.start()) pick_.searched(false, ip, port, now);
  }

  void connect() {
    DSettings& s = ctx_.s;
    host_ = pick_.host();
    mqtt_.setServer(host_.c_str(), pick_.port());
    std::string id = std::string("donoff2-") + s.devId;
    std::string will = base_ + "out/status";
    if (!mqtt_.connect(id.c_str(), s.mqttUser, s.mqttPass, will.c_str(), 0, true, "offline")) return;
    mqtt_.subscribe((base_ + "in/params").c_str());
    pub("out/status", "online", true);
    pub("out/meta", meta(), true);
    published_ = false;
  }

  std::string meta() {
    std::string j = std::string("{\"type\":\"") + dev_.type() + "\",\"fw\":\"" + fw_ + "\",\"relays\":" + std::to_string(dev_.relays().size()) + ",\"sensors\":[";
    bool first = true;
    for (auto* s : dev_.sensors())
      for (size_t i = 0; i < s->count(); i++) {
        DValue& v = s->value(i);
        j += std::string(first ? "" : ",") + "{\"name\":\"" + v.name + "\",\"unit\":\"" + v.unit + "\",\"mult\":" + std::to_string(v.mult) + ",\"reg\":" + std::to_string(v.mbReg) + "}";
        first = false;
      }
    return j + "]}";
  }

  static std::string hms(uint32_t ms) {
    uint32_t s = ms / 1000;
    char b[16];
    snprintf(b, sizeof b, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    return b;
  }

  void publishAll() {
    char b[24], ts[24];
    time_t t = time(nullptr);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", localtime(&t));
    for (auto* s : dev_.sensors())
      for (size_t i = 0; i < s->count(); i++) {
        DValue& v = s->value(i);
        v.format(b, sizeof b);
        pub(std::string("out/sensors/") + v.name, b);
        if (ctx_.clock.hasTime() && v.ok())
          pub(std::string("out/sensors/") + v.name + "_json", std::string("{\"timestamp\":\"") + ts + "\",\"s_type\":\"" + std::to_string(v.sType) + "\",\"name\":\"" + (v.label ? v.label : v.name) + "\",\"val\":\"" + std::to_string(v.raw) + "\",\"mult\":" + std::to_string(v.mult) + "}");
      }
    for (auto* s : dev_.sensors()) {
      if (!s->group() || !ctx_.clock.hasTime()) continue;
      std::string j = std::string("{\"timestamp\":\"") + ts + "\"";
      for (size_t i = 0; i < s->count(); i++) {
        DValue& v = s->value(i);
        v.format(b, sizeof b);
        j += std::string(",\"") + (v.key ? v.key : v.name) + "\":\"" + b + "\"";
      }
      pub(std::string("out/sensors/") + s->group(), j + "}");
    }
    uint32_t now = millis();
    auto& rel = dev_.relays();
    for (auto* r : rel) pub(std::string("out/relays/") + r->name(), r->isOn() ? "1" : "0");
    pub("out/time_up", hms(now));
    for (size_t i = 0; i < rel.size(); i++) {
      if (i > 0 && rel.size() <= 2) break;  // like the old firmware: r2 of a socket has no timers
      std::string n = i ? std::to_string(i + 1) : "";
      pub("out/time_comm" + n, rel[i]->isOn() ? hms(now - rel[i]->startMs()) : "OFF");
      pub("out/time_down" + n, rel[i]->isOn() ? "00:00:00" : hms(now - rel[i]->stopMs()));
    }
    rememberRelays();
  }

  void rememberRelays() {
    auto& rel = dev_.relays();
    seen_.resize(rel.size());
    for (size_t i = 0; i < rel.size(); i++) seen_[i] = rel[i]->isOn();
  }

  // A relay is reported at once, not with the next round.
  void publishRelayChanges() {
    auto& rel = dev_.relays();
    if (seen_.size() != rel.size()) return;
    for (size_t i = 0; i < rel.size(); i++) {
      if ((seen_[i] != 0) == rel[i]->isOn()) continue;
      seen_[i] = rel[i]->isOn();
      pub(std::string("out/relays/") + rel[i]->name(), seen_[i] ? "1" : "0");
    }
  }

  const char* fw_;
  WiFiClient net_;
  PubSubClient mqtt_;
  std::string base_, host_;
  DBrokerPick pick_;
  DBrokerFinder finder_;
  std::deque<std::string> inbox_;
  std::vector<uint8_t> seen_;
  uint32_t lastTry_ = 0, lastPub_ = 0, fails_ = 0;
  bool tried_ = false, published_ = false, up_ = false;
};
