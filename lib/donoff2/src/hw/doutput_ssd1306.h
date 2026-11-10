// The small SSD1306 screen: relays, then the sensors while they fit.
#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "../doutput.h"

class DOutputSSD1306 : public DOutput {
 public:
  DOutputSSD1306(DContext& ctx, DDevice& dev, uint8_t sda, uint8_t scl)
      : DOutput(ctx, "display", dev), sda_(sda), scl_(scl), oled_(128, 64, &Wire, -1) {}

  void begin() override {
    Wire.begin(sda_, scl_);
    ok_ = oled_.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (!ok_) return;
    oled_.clearDisplay();
    oled_.setTextSize(1);
    oled_.setTextColor(SSD1306_WHITE);
    oled_.setCursor(0, 0);
    oled_.println("hi, donoff");
    oled_.display();
  }

  void loop() override {
    uint32_t now = ctx_.clock.ms();
    if (!ok_ || now - last_ < 1000) return;
    last_ = now;
    oled_.clearDisplay();
    oled_.setCursor(0, 0);
    uint8_t lines = 0;
    for (auto* r : dev_.relays()) {
      oled_.printf("%s:%s\n", r->name(), r->isOn() ? "on" : "off");
      lines++;
    }
    char b[24];
    for (auto* s : dev_.sensors())
      for (size_t i = 0; i < s->count() && lines < kLines; i++, lines++) {
        DValue& v = s->value(i);
        v.format(b, sizeof b);
        oled_.printf("%s:%s\n", v.name, v.ok() ? b : "nc");
      }
    oled_.display();
  }

 private:
  static constexpr uint8_t kLines = 6;
  uint8_t sda_, scl_;
  Adafruit_SSD1306 oled_;
  uint32_t last_ = 0;
  bool ok_ = false;
};
