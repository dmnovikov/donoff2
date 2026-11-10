// Modbus TCP server, one client at a time: a new client replaces the old one.
#pragma once
#include "dplatform.h"
#include "../doutput_modbus.h"

class DOutputModbusTCP : public DOutputModbus {
 public:
  DOutputModbusTCP(DContext& ctx, DDevice& dev) : DOutputModbus(ctx, "modbus", dev), server_(502) {}

  void loop() override {
    if (!ctx_.s.mbtcp || WiFi.status() != WL_CONNECTED) return;
    if (!started_) {
      started_ = true;
      server_.begin((uint16_t)ctx_.s.mbport);
    }
    WiFiClient fresh = server_.accept();
    if (fresh) {
      client_.stop();
      client_ = fresh;
      client_.setNoDelay(true);
      have_ = 0;
    }
    if (!client_ || !client_.connected()) return;
    while (client_.available() && have_ < sizeof in_) {
      in_[have_++] = client_.read();
      if (have_ >= 7) {
        size_t len = (in_[4] << 8) | in_[5];  // the unit byte and the PDU
        if (in_[2] || in_[3] || len < 2 || len > kMaxPdu + 1) {
          client_.stop();
          have_ = 0;
          return;
        }
        if (have_ == 6 + len) {
          respond(len - 1);
          have_ = 0;
        }
      }
    }
  }

 private:
  static constexpr size_t kMaxPdu = 253;

  void respond(size_t pdu) {
    uint8_t out[7 + kMaxPdu];
    size_t n;
    if (in_[6] == ctx_.s.mbunit || in_[6] == 0xFF) {
      n = answer(in_ + 7, pdu, out + 7);
    } else {
      out[7] = in_[7] | 0x80;
      out[8] = 0x0B;  // the unit does not answer
      n = 2;
    }
    for (uint8_t i = 0; i < 4; i++) out[i] = in_[i];
    out[4] = (uint8_t)((n + 1) >> 8);
    out[5] = (uint8_t)(n + 1);
    out[6] = in_[6];
    client_.write(out, n + 7);
  }

  WiFiServer server_;
  WiFiClient client_;
  uint8_t in_[7 + kMaxPdu];
  size_t have_ = 0;
  bool started_ = false;
};
