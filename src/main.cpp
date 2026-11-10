// donoff2: the board is chosen in platformio.ini (BOARD, PINS).
#include <Arduino.h>
#include <EEPROM.h>
#include <dlegacy.h>
#include <hw/dbutton_gpio.h>
#include <hw/dclock_ntp.h>
#include <hw/dled_gpio.h>
#include <hw/doutput_modbus_tcp.h>
#include <hw/doutput_mqtt.h>
#include <hw/dportal_wm.h>
#include <hw/dstorage_littlefs.h>
#include "boards.h"
#ifdef DONOFF_DISPLAY
#include <hw/doutput_ssd1306.h>
#endif

#ifndef BOARD
#define BOARD BoardSocket
#define PINS PINS_V1
#endif

static const char* const kFirmware = "2.0.0-dev";

class EventsSerial : public DEvents {
 public:
  void info(const char* t) override { Serial.printf("INFO %s\n", t); }
  void log(const char* t) override { Serial.printf("LOG %s\n", t); }
};

static DClockNtp gClock;
static EventsSerial gSerial;
static DEventsHub gEvents;
static DSettings gSettings;
static DContext gCtx{gClock, gSettings, gEvents};
static BOARD gBoard(gCtx, PINS);
static DOutputMQTT gMqtt(gCtx, gBoard, kFirmware);
static DOutputModbusTCP gModbus(gCtx, gBoard);
static DStorageLittleFS gStorage;
static DButtonGPIO gButton(gCtx, gBoard.pins().button);
static DLedGPIO gLed(gCtx, gBoard.pins().led);
static DPortalWM gPortal(gCtx, gBoard);
#ifdef DONOFF_DISPLAY
static DOutputSSD1306 gDisplay(gCtx, gBoard, 5, 4);
#endif
static String gLine;

static bool loadLegacy() {
  EEPROM.begin(512);
  uint8_t raw[dlegacy::kSize];
  for (size_t i = 0; i < sizeof raw; i++) raw[i] = EEPROM.read(i);
  EEPROM.end();
  return dlegacy::load(raw, sizeof raw, gSettings);
}

static void buttonLoop() {
  switch (gButton.loop()) {
    case DButton::Press::Short: gBoard.shortPress(); break;
    case DButton::Press::Config: gPortal.start(); break;
    case DButton::Press::Reset: gCtx.rebootWanted = true; break;
    default: break;
  }
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
  gButton.begin();
  gLed.begin();
  gClock.begin(gSettings.tz);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();  // the network saved in the chip
  gMqtt.begin();
  gModbus.begin();
#ifdef DONOFF_DISPLAY
  gDisplay.begin();
#endif
}

void loop() {
  gBoard.loop();
  gMqtt.loop();
  gModbus.loop();
  gPortal.loop();
  buttonLoop();
  gLed.setPattern(gPortal.active() ? DLed::Blink : gMqtt.connected() ? DLed::Dark : DLed::Offline);
  gLed.loop();
#ifdef DONOFF_DISPLAY
  gDisplay.loop();
#endif
  gClock.setTz(gSettings.tz);
  if (gCtx.rebootWanted) {
    if (gCtx.saveWanted) gBoard.saveNow();
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
