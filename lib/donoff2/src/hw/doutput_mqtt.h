// The topics of donoff-lib, plus two new ones: out/meta (what the device is) and out/status (Last Will).
#pragma once
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <deque>
#include <string>
#include <time.h>
#include "../doutput.h"

class DOutputMQTT : public DOutput, public DEvents {
 public:
  DOutputMQTT(DContext& ctx, DDevice& dev, const char* fw) : DOutput(ctx, "mqtt", dev), fw_(fw), mqtt_(net_) {}

  void begin() override {
    DSettings& s = ctx_.s;
    base_ = std::string("/") + s.mqttUser + "/" + s.devId + "/";
    mqtt_.setServer(s.mqttServer, atoi(s.mqttPort));
    mqtt_.setBufferSize(1024);
    mqtt_.setSocketTimeout(3);
    mqtt_.setCallback([this](char*, uint8_t* p, unsigned int n) {
      if (inbox_.size() < kInbox) inbox_.emplace_back((const char*)p, n);
    });
  }

  void loop() override {
    if (WiFi.status() != WL_CONNECTED) return;
    uint32_t now = millis();
    if (!mqtt_.connected()) {
      if (tried_ && now - lastTry_ < kRetryMs) return;
      tried_ = true;
      lastTry_ = now;
      connect();
      return;
    }
    mqtt_.loop();
    while (!inbox_.empty()) {
      std::string cmd = inbox_.front();
      inbox_.pop_front();
      dev_.commands().handle(cmd.c_str());
    }
    if (published_ && now - lastPub_ < kPublishMs) return;
    published_ = true;
    lastPub_ = now;
    publishAll();
  }

  void info(const char* t) override { pub("out/info", t); }
  void log(const char* t) override { pub("out/log", t); }

 private:
  static constexpr uint32_t kRetryMs = 10000, kPublishMs = 8000;
  static constexpr size_t kInbox = 4;

  void pub(const std::string& sub, const std::string& v, bool retain = false) {
    if (mqtt_.connected()) mqtt_.publish((base_ + sub).c_str(), v.c_str(), retain);
  }

  void connect() {
    DSettings& s = ctx_.s;
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
        j += std::string(first ? "" : ",") + "{\"name\":\"" + v.name + "\",\"unit\":\"" + v.unit + "\",\"mult\":" + std::to_string(v.mult) + "}";
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
          pub(std::string("out/sensors/") + v.name + "_json", std::string("{\"timestamp\":\"") + ts + "\",\"val\":\"" + std::to_string(v.raw) + "\",\"mult\":" + std::to_string(v.mult) + "}");
      }
    uint32_t now = millis();
    auto& rel = dev_.relays();
    for (auto* r : rel) pub(std::string("out/relays/") + r->name(), r->isOn() ? "1" : "0");
    pub("out/time_up", hms(now));
    if (!rel.empty()) {
      pub("out/time_comm", rel[0]->isOn() ? hms(now - rel[0]->startMs()) : "OFF");
      pub("out/time_down", rel[0]->isOn() ? "00:00:00" : hms(now - rel[0]->stopMs()));
    }
  }

  const char* fw_;
  WiFiClient net_;
  PubSubClient mqtt_;
  std::string base_;
  std::deque<std::string> inbox_;
  uint32_t lastTry_ = 0, lastPub_ = 0;
  bool tried_ = false, published_ = false;
};
