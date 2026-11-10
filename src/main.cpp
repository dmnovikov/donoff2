// donoff2 on the bench socket.
#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ddevice.h>
#include <dlegacy.h>
#include <hw/dclock_ntp.h>
#include <hw/doutput_mqtt.h>
#include <hw/drelay_gpio.h>
#include <hw/dsensor_ds18b20.h>
#include <hw/dstorage_littlefs.h>

static const char* const kFirmware = "2.0.0-dev";

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

static DClockNtp gClock;
static EventsSerial gSerial;
static DEventsHub gEvents;
static DSettings gSettings;
static DContext gCtx{gClock, gSettings, gEvents};
static BoardBench gBoard(gCtx);
static DOutputMQTT gMqtt(gCtx, gBoard, kFirmware);
static DStorageLittleFS gStorage;
static String gLine;

static bool loadLegacy() {
  EEPROM.begin(512);
  uint8_t raw[dlegacy::kSize];
  for (size_t i = 0; i < sizeof raw; i++) raw[i] = EEPROM.read(i);
  EEPROM.end();
  return dlegacy::load(raw, sizeof raw, gSettings);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  gEvents.add(&gSerial);
  gEvents.add(&gMqtt);
  bool fs = gStorage.begin();
  gBoard.setStorage(&gStorage);
  bool stored = fs && gBoard.loadSettings();
  bool legacy = false;
  if (!stored) {
    legacy = loadLegacy();
    if (legacy) gCtx.saveWanted = true;  // moved over once; the old sector stays for a way back
  }
  Serial.printf("\nDONOFF2 %s fs=%d stored=%d legacy=%d dev=%s\n", kFirmware, fs, stored, legacy, gSettings.devId);
  gBoard.begin();
  gClock.begin(gSettings.tz);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();  // the network saved in the chip
  gMqtt.begin();
}

void loop() {
  gBoard.loop();
  gMqtt.loop();
  gClock.setTz(gSettings.tz);
  if (gCtx.rebootWanted) {
    delay(200);
    ESP.restart();
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      gBoard.commands().handle(gLine.c_str());
      gLine = "";
    } else {
      gLine += c;
    }
  }
}
