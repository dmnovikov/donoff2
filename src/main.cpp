// donoff2 on the bench: settings from the old EEPROM (read only), commands over the serial port.
#include <Arduino.h>
#include <EEPROM.h>
#include <ddevice.h>
#include <dlegacy.h>
#include <hw/drelay_gpio.h>
#include <hw/dsensor_ds18b20.h>

class ClockArduino : public DClock {
 public:
  uint32_t ms() const override { return millis(); }
  DTimeState timeState() const override { return DTimeState::None; }  // NTP comes with the network
  uint8_t hour() const override { return (millis() / 3600000UL) % 24; }
  uint8_t minute() const override { return (millis() / 60000UL) % 60; }
  int year() const override { return 1970; }
};

class EventsSerial : public DEvents {
 public:
  void info(const char* t) override { Serial.printf("INFO %s\n", t); }
  void log(const char* t) override { Serial.printf("LOG %s\n", t); }
};

// Bench socket: PINS_SET_V1, the DS18B20 on GPIO4.
class BoardBench : public DDevice {
 public:
  explicit BoardBench(DContext& c) : DDevice(c, "socket2") {}

 protected:
  void build() override {
    auto* temp = new DSensorDS18B20(ctx_, "temp_in", 4);
    sensors_.push_back(temp);
    auto* r1 = new DRelayGPIO(ctx_, "r1", 5);
    auto* r2 = new DRelayGPIO(ctx_, "r2", 12);
    relays_ = {r1, r2};
    DSettings& s = ctx_.s;
    rules_ = {new DRuleHotter(ctx_, *r1, &temp->value(0)),
              new DRuleCooler(ctx_, *r1, &temp->value(0)),
              new DRuleSchedule(ctx_, *r1, &s.lschm1, &s.aofs1, &s.nosync1, true),
              new DRuleSchedule(ctx_, *r2, &s.lschm2, &s.aofs2, &s.nosync2, false),
              new DRuleAutoStop(ctx_, *r1, &s.aofs1, true),
              new DRuleAutoStop(ctx_, *r2, &s.aofs2, false)};
  }
};

static ClockArduino gClock;
static EventsSerial gEvents;
static DSettings gSettings;
static DContext gCtx{gClock, gSettings, gEvents};
static BoardBench gBoard(gCtx);
static String gLine;
static uint32_t gLastPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  EEPROM.begin(512);
  uint8_t raw[dlegacy::kSize];
  for (size_t i = 0; i < sizeof raw; i++) raw[i] = EEPROM.read(i);
  EEPROM.end();
  bool old = dlegacy::load(raw, sizeof raw, gSettings);
  Serial.printf("\nDONOFF2 start, legacy settings %s, dev %s, lschm %ld/%ld\n", old ? "ok" : "none", gSettings.devId, (long)gSettings.lschm1, (long)gSettings.lschm2);
  gBoard.begin();
}

void loop() {
  gBoard.loop();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      gBoard.commands().handle(gLine.c_str());
      gLine = "";
    } else {
      gLine += c;
    }
  }
  if (millis() - gLastPrint >= 5000) {
    gLastPrint = millis();
    char b[24];
    DValue* v = gBoard.findValue("temp_in");
    v->format(b, sizeof b);
    Serial.printf("VALUE temp_in=%s\n", b);
  }
}
