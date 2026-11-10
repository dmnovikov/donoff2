// The boards. A board only says what is on it: every class of donoff-lib's supplies is one class here.
#pragma once
#include <ddevice.h>
#include <hw/dplatform.h>
#include <hw/drelay_gpio.h>
#include <hw/dsensor_ads1115.h>
#include <hw/dsensor_bh1750.h>
#include <hw/dsensor_bme280.h>
#include <hw/dsensor_dht.h>
#include <hw/dsensor_ds18b20.h>
#include <hw/dsensor_max44009.h>
#include <hw/dsensor_pzem.h>
#include <hw/dsensor_sct013.h>

// The Modbus map: fixed by hand, a new sensor must not move the others. Every value takes 3 registers.
namespace dreg {
enum : uint16_t {
  TempIn = 10, TempOut = 13, TempOut2 = 16, TempOdd = 19,
  Current = 22, StrongSec = 25,
  Pzem = 30,        // 4 values: 30..41
  Dht = 42,         // 2 values: 42..47
  Lux = 48,
  Ads = 51,         // 4 values: 51..62
  Sct3 = 63,        // 4 values: 63..74
  Temps = 75,       // 5 values: 75..89
  Bme = 90,         // 3 values: 90..98
};
}

struct DPins {
  const char* pinset;
  uint8_t button, led;
  int8_t r1, r2, tIn, tOut;
};
#define PINS_V1 DPins{"pinsver=v1", 0, 2, 5, 12, 14, 4}
#define PINS_V2 DPins{"pinsver=v2", 0, 2, 12, 5, 14, 13}
#define PINS_V3 DPins{"pinsver=v3", 15, 2, 12, 5, 14, 13}
// V1 with one relay: GPIO12 (D6) of relay 2 is free for I2C.
#define PINS_V1_1R DPins{"pinsver=v1", 0, 2, 5, -1, 14, -1}
#define PINS_ESP32R4 DPins{"", 39, 2, 25, 26, -1, -1}

// The I2C bus of every I2C board: D2 and D7 by default. An env can move the default (-DI2C_SDA=, -DI2C_SCL=),
// the settings i2csda and i2cscl move it on a device.
#ifndef I2C_SDA
#define I2C_SDA 4
#endif
#ifndef I2C_SCL
#define I2C_SCL 13
#endif

class DBoard : public DDevice, public DCmdExtra {
 public:
  DBoard(DContext& c, const char* type, const char* info, const DPins& pins) : DDevice(c, type), pins_(pins), info_(info) {
    commands().setExtra(this);
  }
  const DPins& pins() const { return pins_; }

  bool pinBusy(long p, const std::string& name) override {
    if ((name == "i2cscl" && p == sda_) || (name == "i2csda" && p == scl_)) return true;
    return pinBusy(p);
  }
  bool pinBusy(long p) {
    if (p == pins_.button || p == pins_.led || p == pins_.r1 || p == pins_.r2 || p == pins_.tIn || p == pins_.tOut) return true;
#ifdef ESP32
    return p < 0 || p > 33 || (p >= 6 && p <= 11) || p == 20 || p == 24 || (p >= 28 && p <= 31);  // 34..39 only inputs
#else
    return p < 0 || (p >= 6 && p <= 11) || p > 15;  // 6..11 are the flash, GPIO16 has no open drain
#endif
  }

  bool extraShow(const std::string& what, std::string& out) override {
    if (what == "net") out = "SSID=" + str(WiFi.SSID()) + ",rss=" + std::to_string(WiFi.RSSI()) + ",ip=" + str(WiFi.localIP().toString());
    else if (what == "ip") out = str(WiFi.localIP().toString());
    else if (what == "pinset") out = pins_.pinset;
    else if (what == "dinfo") out = info_;
    else if (what == "i2c") out = "i2c sda=" + std::to_string(sda_) + " scl=" + std::to_string(scl_);
    else if (what == "cmpl")
      out = std::string("cmpl=R2:") + (relays_.size() > 1 ? "1" : "0") + ";DS_OUT:" + (findValue("temp_out") ? "1" : "0") + ";DS_INT:" + (findValue("temp_in") ? "1" : "0") + ";";
    else return false;
    return true;
  }

 protected:
  static std::string str(const String& s) { return s.c_str(); }
  // The I2C pins: the setting if it is usable on this board, otherwise the default of the board.
  uint8_t i2cPin(int32_t set, uint8_t def, bool sda) {
    uint8_t p = def;
    if (set >= 0 && !pinBusy(set)) p = (uint8_t)set;
    else if (set >= 0) ctx_.ev.log("E:i2c pin busy, default used");
    (sda ? sda_ : scl_) = p;
    return p;
  }
  uint8_t sda(uint8_t def = I2C_SDA) { return i2cPin(ctx_.s.i2csda, def, true); }
  uint8_t scl(uint8_t def = I2C_SCL) { return i2cPin(ctx_.s.i2cscl, def, false); }
  int sda_ = -1, scl_ = -1;
  DPins pins_;
  const char* info_;
};

// Only sensors, the button only opens the portal.
class BoardCollector : public DBoard {
 public:
  using DBoard::DBoard;
};

// donoff: relay 1, relay 2, two DS18B20, thermostat and schedules. Others extend it.
class BoardSocket : public DBoard {
 public:
  BoardSocket(DContext& c, const DPins& p, const char* info = "SUPPLY_UNIVERSAL") : DBoard(c, p.r2 >= 0 ? "socket2" : "socket", info, p) {}

  void shortPress() override {  // the hand wins over every automatic
    DSettings& s = ctx_.s;
    s.hotter = 0;
    s.cooler = 0;
    s.lschm1 = 0;
    s.aofs1 = 0;
    DRelay* r = relays_[0];
    if (r->isOn()) r->off("hardware");
    else r->on("hardware");
  }

  bool extraShow(const std::string& what, std::string& out) override {
    if (what != "hot") return DBoard::extraShow(what, out);
    DSettings& s = ctx_.s;
    if (s.hotter == 0) {
      out = "I: hotter=0";
      return true;
    }
    if (!tempOut_) {
      out = "I: no sensor";
      return true;
    }
    char b[16];
    tempOut_->value(0).format(b, sizeof b);
    out = std::to_string(s.hotter) + ";s=" + b + ";l=" + std::to_string(hotter_->levelSetting()) + ";td=" + std::to_string(s.tdelta);
    return true;
  }

 protected:
  void build() override {
    DSettings& s = ctx_.s;
    auto* r1 = new DRelayGPIO(ctx_, "r1", pins_.r1);
    relays_.push_back(r1);
    if (pins_.tIn >= 0) sensors_.push_back(new DSensorDS18B20(ctx_, "temp_in", pins_.tIn, dreg::TempIn, false, "ds_in"));
    if (pins_.tOut >= 0) {
      tempOut_ = new DSensorDS18B20(ctx_, "temp_out", pins_.tOut, dreg::TempOut, true, "ds_out");
      sensors_.push_back(tempOut_);
      hotter_ = new DRuleHotter(ctx_, *r1, &tempOut_->value(0));
      rules_.push_back(hotter_);
      rules_.push_back(new DRuleCooler(ctx_, *r1, &tempOut_->value(0)));
    }
    rules_.push_back(new DRuleSchedule(ctx_, *r1, &s.lschm1, &s.aofs1, &s.nosync1, true));
    rules_.push_back(new DRuleAutoStop(ctx_, *r1, &s.aofs1, true));
    if (pins_.r2 >= 0) {
      auto* r2 = new DRelayGPIO(ctx_, "r2", pins_.r2);
      relays_.push_back(r2);
      rules_.push_back(new DRuleSchedule(ctx_, *r2, &s.lschm2, &s.aofs2, &s.nosync2, false));
      rules_.push_back(new DRuleAutoStop(ctx_, *r2, &s.aofs2, false));
    }
    extras();
  }
  virtual void extras() {}

  DSensorDS18B20* tempOut_ = nullptr;
  DRuleHotter* hotter_ = nullptr;
};

class BoardSocketSingle : public BoardSocket {
 public:
  BoardSocketSingle(DContext& c, const DPins& p) : BoardSocket(c, noR2(p), "SUPPLY_DONOFF") {}

 private:
  static DPins noR2(DPins p) {
    p.r2 = -1;
    p.tOut = -1;
    return p;
  }
};

// One current clamp on top of the socket.
class BoardSocketCurrent : public BoardSocket {
 public:
  BoardSocketCurrent(DContext& c, const DPins& p, const char* info = "SUPPLY_UNI_SCT013") : BoardSocket(c, p, info) {}

 protected:
  void extras() override {
    current_ = new DSensorSCT013(ctx_, "current", A0, dreg::Current);
    sensors_.push_back(current_);
  }
  DSensorSCT013* current_ = nullptr;
};

// The pump: strong_sec counts while the current is above clevel1, and after clevel2 seconds relay 2 stops.
class BoardPump : public BoardSocketCurrent {
 public:
  BoardPump(DContext& c, const DPins& p) : BoardSocketCurrent(c, p, "SUPPLY_PUMP") {}

 protected:
  void extras() override {
    BoardSocketCurrent::extras();
    DSettings& s = ctx_.s;
    if (s.clevel[0] == -128) s.clevel[0] = 100;   // 1 A
    if (s.clevel[1] == -128) s.clevel[1] = 3600;
    auto* over = new DSensorOverTime(ctx_, "strong_sec", &current_->value(0), &s.clevel[0], dreg::StrongSec);
    sensors_.push_back(over);
    rules_.push_back(new DRuleLimiter(ctx_, *relays_[1], &over->value(0), &s.clevel[1], &s.lschm2));
  }
};

// A third DS18B20 on D1; relay 2 lives on that pin, so there is none.
class BoardSocketDs2 : public BoardSocket {
 public:
  BoardSocketDs2(DContext& c, const DPins& p, const char* info = "SUPPLY_DS2") : BoardSocket(c, noR2(p), info) {}

 protected:
  void extras() override {
    out2_ = new DSensorDS18B20(ctx_, "temp_out2", 5, dreg::TempOut2, true, "ds_out2");
    sensors_.push_back(out2_);
  }
  DSensorDS18B20* out2_ = nullptr;

 private:
  static DPins noR2(DPins p) {
    p.r2 = -1;
    return p;
  }
};

class BoardTempOdds : public BoardSocketDs2 {
 public:
  BoardTempOdds(DContext& c, const DPins& p) : BoardSocketDs2(c, p, "SUPPLY_TEMP_ODDS") {}

 protected:
  void extras() override {
    BoardSocketDs2::extras();
    sensors_.push_back(new DSensorDifference(ctx_, "temp_odd", &tempOut_->value(0), &out2_->value(0), dreg::TempOdd));
  }
};

class BoardSocketDht : public BoardSocket {
 public:
  BoardSocketDht(DContext& c, const DPins& p) : BoardSocket(c, single(p), "SUPPLY_DHT") {}

 protected:
  void extras() override { sensors_.push_back(new DSensorDHT(ctx_, 4, dreg::Dht)); }

 private:
  static DPins single(DPins p) {
    p.r2 = -1;
    p.tOut = -1;
    return p;
  }
};

// I2C takes D2 and D7, D7 is the bus of temp_out.
class BoardSocketAds : public BoardSocket {
 public:
  BoardSocketAds(DContext& c, const DPins& p) : BoardSocket(c, noOut(p), "SUPPLY_ADS") {}

 protected:
  void extras() override { sensors_.push_back(new DSensorADS1115(ctx_, sda(13), scl(4), dreg::Ads)); }

 private:
  static DPins noOut(DPins p) {
    p.tOut = -1;
    return p;
  }
};

class BoardSocketLux : public BoardSocket {
 public:
  BoardSocketLux(DContext& c, const DPins& p) : BoardSocket(c, noOut(p), "SUPPLY_UNI_MAX44003") {}

 protected:
  void extras() override { sensors_.push_back(new DSensorMAX44009(ctx_, sda(), scl(), dreg::Lux)); }

 private:
  static DPins noOut(DPins p) {
    p.tOut = -1;
    return p;
  }
};


class BoardSocketBh1750 : public BoardSocket {
 public:
  BoardSocketBh1750(DContext& c, const DPins& p) : BoardSocket(c, noOut(p), "SUPPLY_UNI_BH1750") {}

 protected:
  void extras() override { sensors_.push_back(new DSensorBH1750(ctx_, sda(), scl(), dreg::Lux)); }

 private:
  static DPins noOut(DPins p) {
    p.tOut = -1;
    return p;
  }
};

class BoardSocketBme280 : public BoardSocket {
 public:
  BoardSocketBme280(DContext& c, const DPins& p) : BoardSocket(c, noOut(p), "SUPPLY_UNI_BME280") {}

 protected:
  void extras() override { sensors_.push_back(new DSensorBME280(ctx_, sda(), scl(), dreg::Bme)); }

 private:
  static DPins noOut(DPins p) {
    p.tOut = -1;
    return p;
  }
};

class BoardPzem : public BoardCollector {
 public:
  BoardPzem(DContext& c, const DPins& p) : BoardCollector(c, "sensor", "SUPPLY_PZEM004", p) {}

 protected:
  void build() override { sensors_.push_back(new DSensorPZEM(ctx_, 13, 12, dreg::Pzem)); }
};

class BoardSct013 : public BoardCollector {
 public:
  BoardSct013(DContext& c, const DPins& p) : BoardCollector(c, "sensor", "SUPPLY_SCT013_SINGLE", p) {}

 protected:
  void build() override { sensors_.push_back(new DSensorSCT013(ctx_, "sct013_1", A0, dreg::Current)); }
};

class BoardSct013x3 : public BoardCollector {
 public:
  BoardSct013x3(DContext& c, const DPins& p) : BoardCollector(c, "sensor", "SUPPLY_MULTI_SCT013", p) {}

 protected:
  void build() override { sensors_.push_back(new DSensorSCT013x3(ctx_, 12, 13, A0, dreg::Sct3)); }
};

class Board5Temp : public BoardCollector {
 public:
  Board5Temp(DContext& c, const DPins& p) : BoardCollector(c, "sensor", "SUPPLY_6TEMP", p) {}

 protected:
  void build() override {
    static const char* const kNames[] = {"temp1", "temp2", "temp3", "temp4", "temp5"};
    static const char* const kLabels[] = {"ds1", "ds2", "ds3", "ds4", "ds5"};
    static const uint8_t kPins[] = {13, 12, 14, 5, 4};
    for (uint8_t i = 0; i < 5; i++) sensors_.push_back(new DSensorDS18B20(ctx_, kNames[i], kPins[i], dreg::Temps + 3 * i, true, kLabels[i]));
  }
};

// ESP32, four relays: clevel1/2 are the schedules of r3/r4, clevel3/4 their watering seconds.
class BoardEsp32R4 : public DBoard {
 public:
  BoardEsp32R4(DContext& c, const DPins& p) : DBoard(c, "relay4", "SUPPLY_ESP32R4", p) {
    DSettings& s = ctx_.s;
    using P = DCommands;
    commands().addParam({"lschm3", nullptr, nullptr, &s.clevel[0], 0, 100, P::RelayScheme, 2});
    commands().addParam({"lschm4", nullptr, nullptr, &s.clevel[1], 0, 100, P::RelayScheme, 3});
    commands().addParam({"aofs3", nullptr, nullptr, &s.clevel[2], 0, 1200, P::Plain});
    commands().addParam({"aofs4", nullptr, nullptr, &s.clevel[3], 0, 1200, P::Plain});
    commands().addParam({"nosync3", nullptr, nullptr, &s.nosync3, 0, 1, P::Plain});
    commands().addParam({"nosync4", nullptr, nullptr, &s.nosync4, 0, 1, P::Plain});
  }

 protected:
  void build() override {
    DSettings& s = ctx_.s;
    for (auto& c : s.clevel)
      if (c == -128) c = 0;
    static const char* const kNames[] = {"r1", "r2", "r3", "r4"};
    static const uint8_t kPins[] = {25, 26, 33, 32};
    for (uint8_t i = 0; i < 4; i++) relays_.push_back(new DRelayGPIO(ctx_, kNames[i], kPins[i]));
    struct { int32_t* scheme; int32_t* sec; int32_t* nosync; } t[] = {
        {&s.lschm1, &s.aofs1, &s.nosync1}, {&s.lschm2, &s.aofs2, &s.nosync2},
        {&s.clevel[0], &s.clevel[2], &s.nosync3}, {&s.clevel[1], &s.clevel[3], &s.nosync4}};
    for (uint8_t i = 0; i < 4; i++) {
      rules_.push_back(new DRuleSchedule(ctx_, *relays_[i], t[i].scheme, t[i].sec, t[i].nosync, false));
      rules_.push_back(new DRuleAutoStop(ctx_, *relays_[i], t[i].sec, false));
    }
  }
};
