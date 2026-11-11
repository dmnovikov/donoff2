// Tests of the donoff2 core: no hardware, a fake clock, a fake relay, a fake sensor.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ddevice.h"
#include "dbutton.h"
#include "dled.h"
#include "dlegacy.h"
#include "doutput_modbus.h"
#include "dpzem.h"
#include "djsy.h"

static int gFails = 0, gChecks = 0;
#define CHECK(c)                                              \
  do {                                                        \
    gChecks++;                                                \
    if (!(c)) {                                               \
      gFails++;                                               \
      printf("FAIL %s:%d  %s\n", __func__, __LINE__, #c);     \
    }                                                         \
  } while (0)

struct FakeClock : DClock {
  uint32_t now = 0;
  DTimeState st = DTimeState::Synced;
  uint8_t h = 12;
  uint32_t ms() const override { return now; }
  DTimeState timeState() const override { return st; }
  uint8_t hour() const override { return h; }
  uint8_t minute() const override { return 5; }
  int year() const override { return 2026; }
};

struct FakeEvents : DEvents {
  std::vector<std::string> infos, logs;
  void info(const char* t) override { infos.push_back(t); }
  void log(const char* t) override { logs.push_back(t); }
  std::string lastInfo() const { return infos.empty() ? "" : infos.back(); }
  std::string lastLog() const { return logs.empty() ? "" : logs.back(); }
};

struct FakeRelay : DRelay {
  using DRelay::DRelay;
  void write(bool) override {}
};

// One piece of hardware, two values: the shape of BMP280 or PZEM.
struct FakeMeter : DSensor {
  float temp = 0, press = 0;
  bool alive = true;
  int reads = 0;
  FakeMeter(DContext& c) : DSensor(c, "meter", 1000) {
    addValue("temp_in", 100, "C", 10);
    addValue("press", 10, "hPa", 13);
  }
  bool collect() override {
    reads++;
    if (!alive) return false;
    value(0).set(temp);
    value(1).set(press);
    return true;
  }
};

struct AskedSensor : DSensor {
  int asked = 0, read = 0;
  AskedSensor(DContext& c) : DSensor(c, "ds", 5000, 800) { addValue("t", 100, "C"); }
  void request() override { asked++; }
  bool collect() override {
    read++;
    value(0).set(21.5f);
    return true;
  }
};

struct Bed {
  FakeClock clock;
  FakeEvents ev;
  DSettings s;
  DContext ctx{clock, s, ev};
};

struct Board : DDevice {
  FakeMeter* meter = nullptr;
  FakeRelay* r1 = nullptr;
  FakeRelay* r2 = nullptr;
  explicit Board(DContext& c) : DDevice(c, "socket2") {}
  void build() override {
    meter = new FakeMeter(ctx_);
    sensors_.push_back(meter);
    r1 = new FakeRelay(ctx_, "r1");
    r2 = new FakeRelay(ctx_, "r2");
    relays_ = {r1, r2};
    DSettings& s = ctx_.s;
    rules_ = {new DRuleHotter(ctx_, *r1, &meter->value(0)),
              new DRuleCooler(ctx_, *r1, &meter->value(0)),
              new DRuleSchedule(ctx_, *r1, &s.lschm1, &s.aofs1, &s.nosync1, true),
              new DRuleSchedule(ctx_, *r2, &s.lschm2, &s.aofs2, &s.nosync2, false),
              new DRuleAutoStop(ctx_, *r1, &s.aofs1, true),
              new DRuleAutoStop(ctx_, *r2, &s.aofs2, false)};
  }
};

struct Rig : Bed {
  Board b{ctx};
  Rig() { b.begin(); }
  void step(uint32_t ms = 1100) {
    clock.now += ms;
    b.loop();
  }
  void temp(float t) {
    b.meter->temp = t;
    step();
    step();
  }
  void cmd(const char* c) {
    clock.now += 1100;
    b.commands().handle(c);
  }
};

static void valueFormat() {
  DValue v("a", 100, "C", 0);
  char b[24];
  v.format(b, sizeof b);
  CHECK(std::string(b) == "-128.00");
  v.fail();
  v.format(b, sizeof b);
  CHECK(std::string(b) == "-127.00");
  v.set(-3.05f);
  v.format(b, sizeof b);
  CHECK(std::string(b) == "-3.05");
  DValue s("sec", 1, "s", 0);
  s.setRaw(60);
  s.format(b, sizeof b);
  CHECK(std::string(b) == "60");
}

static void multiValueSensor() {
  Rig r;
  r.b.meter->temp = 21.5f;
  r.b.meter->press = 1013.2f;
  r.step();
  CHECK(r.b.findValue("temp_in")->raw == 2150);
  CHECK(r.b.findValue("press")->raw == 10132);
  CHECK(r.b.findValue("nothing") == nullptr);
  r.b.meter->alive = false;
  r.step();
  CHECK(r.b.findValue("temp_in")->state == DValueState::Error);
  CHECK(r.b.findValue("press")->state == DValueState::Error);
}

static void sensorPeriodAndRequest() {
  Bed t;
  AskedSensor a(t.ctx);
  a.begin();
  a.loop();
  CHECK(a.asked == 1 && a.read == 0);
  t.clock.now = 500;
  a.loop();
  CHECK(a.read == 0);
  t.clock.now = 900;
  a.loop();
  CHECK(a.read == 1 && a.value(0).raw == 2150);
  t.clock.now = 2000;
  a.loop();
  CHECK(a.asked == 1);
  t.clock.now = 5900;
  a.loop();
  CHECK(a.asked == 2);
}

static void hotterHysteresis() {
  Rig r;
  r.s.hotter = 1;
  r.s.dtempl = r.s.ntempl = r.s.ctempl = 20;
  r.s.tdelta = 10;  // tenths: 1 degree
  r.temp(19.5f);
  CHECK(r.b.r1 && r.b.r1->isOn());
  CHECK(r.ev.lastLog() == "L:r1:ON, hotter");
  r.temp(20.5f);
  CHECK(r.b.r1->isOn());
  r.temp(21.0f);
  CHECK(!r.b.r1->isOn());
  r.temp(20.5f);
  CHECK(!r.b.r1->isOn());
}

static void coolerHysteresis() {
  Rig r;
  r.s.cooler = 1;
  r.s.dtempl = r.s.ntempl = 20;
  r.s.tdelta = 100;  // 1/100: 1 degree
  r.temp(20.5f);
  CHECK(r.b.r1->isOn());
  r.temp(19.5f);
  CHECK(r.b.r1->isOn());
  r.temp(19.0f);
  CHECK(!r.b.r1->isOn());
}

static void sensorLostTurnsOff() {
  Rig r;
  r.s.hotter = 1;
  r.s.dtempl = r.s.ntempl = 30;
  r.temp(10);
  CHECK(r.b.r1->isOn());
  r.b.meter->alive = false;
  r.step();
  r.step();
  CHECK(!r.b.r1->isOn());
  CHECK(r.ev.lastLog() == "L:r1:OFF, hotter, sensor not ready");
}

static void levelUnits() {
  CHECK(DRuleTemp::toLevel100(23) == 2300);
  CHECK(DRuleTemp::toLevel100(2350) == 2350);
  CHECK(DRuleTemp::toLevel100(-5) == -500);
  CHECK(DRuleTemp::toLevel100(-500) == -500);  // the old code made -50000 of it
  CHECK(DRuleTemp::toDelta100(10) == 100);
  CHECK(DRuleTemp::toDelta100(50) == 50);
}

static void dayNightAndMatrix() {
  Rig r;
  r.s.hotter = 1;
  r.s.onoffschm = 2;  // always day
  r.s.dtempl = 30;
  r.s.ntempl = 5;
  r.s.ctempl = 5;
  r.temp(20);
  CHECK(r.b.r1->isOn());
  r.s.onoffschm = 1;  // always night
  r.s.tdelta = 10;
  r.temp(20);
  CHECK(!r.b.r1->isOn());

  r.s.hotter = 2;
  r.s.tmatrix[12] = 25;
  r.temp(20);
  CHECK(r.b.r1->isOn());
  r.s.tmatrix[12] = 10;
  r.temp(20);
  CHECK(!r.b.r1->isOn());
}

static void noTimeUsesDefaultLevel() {
  Rig r;
  r.clock.st = DTimeState::None;
  r.s.hotter = 2;
  r.s.ctempl = 25;
  r.s.tmatrix[12] = 5;
  r.temp(20);
  CHECK(r.b.r1->isOn());
}

static void scheduleByHour() {
  Rig r;
  r.s.lschm1 = 99;
  r.s.cschm1 = 1ul << (23 - 12);
  r.step();
  CHECK(r.b.r1->isOn());
  CHECK(r.ev.lastLog() == "L:r1:ON, lschm,on");
  r.clock.h = 13;
  r.step();
  CHECK(!r.b.r1->isOn());
  CHECK(r.ev.lastLog() == "L:r1:OFF, lschm,off");
}

static void wateringOncePerHour() {
  Rig r;
  r.s.lschm2 = 2;
  r.s.aofs2 = 30;
  r.step();
  CHECK(r.b.r2->isOn());
  r.step(31000);
  CHECK(!r.b.r2->isOn());
  r.step(1100);
  r.step(1100);
  CHECK(!r.b.r2->isOn());  // not again in the same hour
  r.clock.h = 13;
  r.step();
  CHECK(r.b.r2->isOn());
}

static void noTimeSchedules() {
  Rig r;
  r.clock.st = DTimeState::None;
  r.s.lschm1 = 2;  // always on needs no clock
  r.s.lschm2 = 5;
  r.step();
  CHECK(r.b.r1->isOn());
  CHECK(!r.b.r2->isOn());
  r.s.nosync2 = 1;
  r.step();
  CHECK(r.b.r2->isOn());
  r.s.nosync2 = 0;
  r.step();
  CHECK(!r.b.r2->isOn());
  CHECK(r.ev.lastLog() == "L:r2:OFF, lschm, no time sync");
}

static void staleTimeKeepsSchedules() {
  Rig r;
  r.clock.st = DTimeState::Stale;
  r.s.lschm1 = 99;
  r.s.cschm1 = 1ul << (23 - 12);
  r.step();
  CHECK(r.b.r1->isOn());
}

static void autoStopManual() {
  Rig r;
  r.s.aofs1 = 10;
  r.cmd("r11");
  r.step();
  CHECK(r.b.r1->isOn());
  r.step(10500);
  CHECK(!r.b.r1->isOn());
  CHECK(r.ev.lastLog() == "L:r1:OFF, aofs,off");
}

static void commandsAnswerLikeOld() {
  Rig r;
  r.cmd("lschm=5");
  CHECK(r.ev.lastInfo() == "N:lschm=5");
  r.cmd("sh lschm1");
  CHECK(r.ev.lastInfo() == "lschm1=5");
  r.cmd("sh lschm");
  CHECK(r.ev.lastInfo() == "lschm=5");
  r.cmd("sh tls");
  CHECK(r.ev.lastInfo() == "C=20,D=18,N=21 dT=10");
  r.cmd("sh time");
  CHECK(r.ev.lastInfo() == "I:S=1,T=12:5:2026");
  r.cmd("sh schma");
  CHECK(r.ev.lastInfo() == "schma=5:0");
  r.cmd("sh nothing");
  CHECK(r.ev.lastInfo() == "E: sh param not recognized");
  r.cmd("nothing=1");
  CHECK(r.ev.lastInfo() == "E:Varable error");
  r.cmd("nothing");
  CHECK(r.ev.lastInfo() == "no command");
  r.cmd("hotter=abc");
  CHECK(r.ev.lastInfo() == "E:not set, INT wait");
  r.cmd("cooler=7");
  CHECK(r.ev.lastInfo() == "E:params error");
}

static void schemesRefused() {
  Rig r;
  for (const char* c : {"lschm=21", "lschm=24", "lschm2=98", "lschm=101"}) {
    r.cmd(c);
    CHECK(r.ev.lastInfo() == "E:params error");
  }
  CHECK(r.s.lschm1 == 0 && r.s.lschm2 == 0);
  r.cmd("lschm=100");
  CHECK(r.s.lschm1 == 100);
}

static void modesExclude() {
  Rig r;
  r.cmd("lschm=2");
  r.cmd("aofs=30");
  r.cmd("hotter=1");
  CHECK(r.s.lschm1 == 0 && r.s.aofs1 == 0 && r.s.hotter == 1);
  r.cmd("cooler=1");
  CHECK(r.s.hotter == 0 && r.s.cooler == 1);
  r.cmd("lschm=2");
  CHECK(r.s.cooler == 0 && r.s.lschm1 == 2);
  r.step();
  CHECK(r.b.r1->isOn());
  r.cmd("lschm=0");
  CHECK(!r.b.r1->isOn());
}

static void savingRules() {
  Rig r;
  r.cmd("ctempl=25");
  CHECK(!r.ctx.saveWanted);
  r.cmd("ctempl=26;");
  CHECK(r.ctx.saveWanted && r.s.ctempl == 26);
  r.ctx.saveWanted = false;
  r.cmd("r11;");  // bare commands do not save
  CHECK(!r.ctx.saveWanted && r.b.r1->isOn());
  r.cmd("ctempl=1;;");  // the ";" is cut only at the end, what is left is not a number
  CHECK(r.ev.lastInfo() == "E:not set, INT wait" && !r.ctx.saveWanted);
  r.cmd("save");
  CHECK(r.ctx.saveWanted);
}

static void antiFlood() {
  Rig r;
  r.cmd("r11");
  r.b.commands().handle("r10");  // the same moment
  CHECK(r.b.r1->isOn());
  CHECK(r.ev.lastInfo() == "DDOS CHECK");
  r.ev.infos.clear();
  r.b.commands().handle("sh hotter");  // questions are always answered
  CHECK(r.ev.lastInfo() == "hotter=0");
}

static void customBits() {
  Rig r;
  r.cmd("cschm1=0");
  CHECK(r.s.cschm1 == 0);
  r.cmd("cschm1=23:1");
  CHECK(r.s.cschm1 == 1);
  CHECK(r.ev.lastInfo() == "N:V[23]=1");
  r.cmd("sh cschm1");
  CHECK(r.ev.lastInfo() == "0000 0000 0000 0000 0000 0001");
  r.cmd("cschm1=1");
  CHECK(r.s.cschm1 == 0xFFFFFF);
  r.cmd("cschm1=24:1");
  CHECK(r.ev.lastInfo() == "E:params error");
  r.cmd("om2=100000000000000000000001");
  CHECK(r.s.cschm2 == 0x800001);
  r.cmd("om2=1");
  CHECK(r.ev.lastInfo() == "E:Var error");
  r.cmd("tmatrix=20");
  CHECK(r.s.tmatrix[0] == 20 && r.s.tmatrix[23] == 20);
  r.cmd("tmatrix=5:18");
  CHECK(r.s.tmatrix[5] == 18);
}

static void shAllGoesToLogOnly() {
  Rig r;
  r.b.meter->temp = 21.5f;
  r.step();
  size_t infos = r.ev.infos.size();
  r.cmd("sh all");
  CHECK(r.ev.infos.size() == infos);
  CHECK(r.ev.lastLog().find("\"temp_in\":21.50") != std::string::npos);
  CHECK(r.ev.lastLog().find("\"lschm\":0") != std::string::npos);
  CHECK(r.ev.lastLog().find("\"r1\":0") != std::string::npos);
  CHECK(r.ev.lastLog().front() == '{' && r.ev.lastLog().back() == '}');
}

struct LimiterBed : Bed {
  FakeMeter meter{ctx};
  FakeRelay relay{ctx, "r2"};
  int32_t scheme = 2;
  int32_t threshold = 100, limit = 60;
  DSensorOverTime over{ctx, "strong_sec", &meter.value(0), &threshold};
  DRuleLimiter rule{ctx, relay, &over.value(0), &limit, &scheme};
  void step(uint32_t ms, float amps) {
    clock.now += ms;
    meter.temp = amps;
    meter.loop();
    over.loop();
    rule.loop();
  }
};

static void limiterTripsAndSaves() {
  LimiterBed t;
  t.meter.begin();
  t.over.begin();
  t.relay.on("");
  t.step(1000, 2.0f);
  t.step(30000, 2.0f);
  CHECK(t.over.value(0).raw == 30);
  CHECK(t.scheme == 2);
  t.step(31000, 2.0f);
  CHECK(t.scheme == 1);
  CHECK(t.ctx.saveWanted);
  CHECK(!t.relay.isOn());
  CHECK(t.ev.lastInfo() == "strong_curr: off, current strong time");
  size_t n = t.ev.infos.size();
  t.step(5000, 2.0f);
  CHECK(t.ev.infos.size() == n);  // said once
}

static void limiterResetsBelowThreshold() {
  LimiterBed t;
  t.meter.begin();
  t.over.begin();
  t.step(1000, 2.0f);
  t.step(50000, 2.0f);
  t.step(1000, 0.5f);
  CHECK(t.over.value(0).raw == 0);
  t.step(1000, 2.0f);
  t.step(50000, 2.0f);
  CHECK(t.scheme == 2);
}

static void limiterOffWhenNoThreshold() {
  LimiterBed t;
  t.threshold = 0;
  t.meter.begin();
  t.over.begin();
  t.step(1000, 50.0f);
  t.step(100000, 50.0f);
  CHECK(t.over.value(0).raw == 0 && t.scheme == 2);
}

static void put32(std::vector<uint8_t>& b, size_t o, int32_t v) { memcpy(&b[o], &v, 4); }

static void legacySettings() {
  std::vector<uint8_t> b(512, 0xFF);
  put32(b, 0, 1206);
  memcpy(&b[4], "broker\0", 7);
  memcpy(&b[26], "user\0", 5);
  memcpy(&b[38], "secret\0", 7);
  memcpy(&b[60], "1883\0", 5);
  memcpy(&b[68], "tav1\0_dev", 9);
  memcpy(&b[193], "a@b.c\0", 6);
  put32(b, 80, 0);
  put32(b, 84, 60);
  b[92] = 0;
  put32(b, 96, 2);
  put32(b, 100, 24);  // a preset that never existed
  put32(b, 104, 5);
  b[165] = 0;
  b[166] = 1;
  put32(b, 168, 23);
  put32(b, 172, 2300);
  put32(b, 176, 99999);  // garbage
  put32(b, 180, 99);
  put32(b, 188, 10);
  put32(b, 328, 150);
  put32(b, 336, 0xFF000001);
  put32(b, 340, 3);
  for (int i = 0; i < 24; i++) put32(b, 344 + 4 * i, 22);
  DSettings s;
  CHECK(dlegacy::load(b.data(), b.size(), s));
  CHECK(std::string(s.mqttServer) == "broker" && std::string(s.mqttUser) == "user" && std::string(s.mqttPass) == "secret");
  CHECK(std::string(s.devId) == "tav1" && std::string(s.email) == "a@b.c");
  CHECK(s.aofs2 == 60 && s.lschm1 == 2 && s.lschm2 == 0 && s.tz == 5);
  CHECK(s.cooler == 1 && s.ctempl == 23 && s.dtempl == 2300 && s.ntempl == 21);
  CHECK(s.clevel[0] == 150 && s.cschm1 == 1 && s.cschm2 == 3 && s.tmatrix[7] == 22);

  DSettings untouched;
  std::vector<uint8_t> bad(512, 0xFF);
  CHECK(!dlegacy::load(bad.data(), bad.size(), untouched));
  CHECK(std::string(untouched.mqttServer) == "mqtt.lab240.ru");
  CHECK(!dlegacy::load(b.data(), 100, untouched));
}

// With DONOFF_EEPROM_SAMPLE=<512 bytes taken from a device> the real thing is read too.
static void legacyRealSample() {
  const char* p = getenv("DONOFF_EEPROM_SAMPLE");
  if (!p) return;
  std::ifstream f(p, std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  DSettings s;
  CHECK(dlegacy::load(b.data(), b.size(), s));
  printf("sample: dev=%s lschm=%ld lschm2=%ld aofs2=%ld C=%ld D=%ld N=%ld dT=%ld tz=%ld\n", s.devId, (long)s.lschm1, (long)s.lschm2, (long)s.aofs2, (long)s.ctempl, (long)s.dtempl, (long)s.ntempl, (long)s.tdelta, (long)s.tz);
}

struct MemStorage : DStorage {
  std::string text;
  bool load(std::string& t) override {
    if (text.empty()) return false;
    t = text;
    return true;
  }
  bool save(const std::string& t) override {
    text = t;
    return true;
  }
};

static void settingsSurviveRestart() {
  MemStorage mem;
  {
    Rig r;
    r.b.setStorage(&mem);
    strcpy(r.s.mqttPass, "p=1");
    strcpy(r.s.devId, "tav1");
    r.cmd("lschm2=16");
    r.cmd("aofs2=60;");
    r.cmd("cschm1=5:1;");
    r.cmd("tmatrix=7:19;");
    r.step();
    CHECK(!r.ctx.saveWanted);
    CHECK(!mem.text.empty());
    CHECK(r.ev.lastInfo() == "N: saved");
  }
  Rig r2;
  r2.s.hotter = 1;  // loading must not run the side effects of commands
  r2.b.setStorage(&mem);
  CHECK(r2.b.loadSettings());
  CHECK(r2.s.lschm2 == 16 && r2.s.aofs2 == 60 && r2.s.hotter == 0);
  CHECK(r2.s.tmatrix[7] == 19 && r2.s.tmatrix[8] == 22);
  CHECK((r2.s.cschm1 >> 18) & 1);
  CHECK(std::string(r2.s.mqttPass) == "p=1" && std::string(r2.s.devId) == "tav1");

  Rig r3;
  r3.b.commands().loadText("lschm=999\nbogus=1\naofs=30\n");
  CHECK(r3.s.lschm1 == 0 && r3.s.aofs1 == 30);
  MemStorage none;
  r3.b.setStorage(&none);
  CHECK(!r3.b.loadSettings());
}

static void medianFilter() {
  DValue v("t", 100, "C");
  v.median = true;
  v.set(20);
  v.set(85);
  CHECK(!v.ok());  // nothing until three readings are in
  v.set(21);
  CHECK(v.ok() && v.raw == 2100);  // the spike is gone
  v.fail();
  v.set(22);
  v.set(23);
  CHECK(v.ok() && v.raw == 2200);  // one lost reading changes nothing
  v.fail();
  v.fail();
  v.set(23);
  CHECK(!v.ok() && v.state == DValueState::Error);
}

static void differenceOfTwo() {
  Bed e;
  DValue a("a", 100, "C"), b("b", 100, "C");
  DSensorDifference d(e.ctx, "odd", &a, &b, 19);
  d.begin();
  d.loop();
  CHECK(!d.value(0).ok());
  a.set(20);
  b.set(23.5f);
  e.clock.now = 2000;
  d.loop();
  CHECK(d.value(0).ok() && d.value(0).raw == 350);
}

struct MbProbe : DOutputModbus {
  using DOutputModbus::DOutputModbus;
  void loop() override {}
};

static void modbusAnswers() {
  Rig r;
  MbProbe mb(r.ctx, "mb", r.b);
  uint8_t o[260];
  r.b.meter->temp = 21.5f;
  r.b.meter->press = 1000;
  r.step();
  r.clock.now = 70000;
  const uint8_t values[] = {4, 0, 10, 0, 3};
  CHECK(mb.answer(values, 5, o) == 8 && o[0] == 4 && o[1] == 6);
  CHECK(o[2] == 0 && o[3] == 0 && o[4] == 0x08 && o[5] == 0x66);  // 2150, two registers, high word first
  CHECK(o[6] == 0 && o[7] == 1);                                  // state ok
  r.b.meter->temp = -5;
  r.step();
  CHECK(mb.answer(values, 5, o) == 8 && o[2] == 0xFF && o[3] == 0xFF && o[4] == 0xFE && o[5] == 0x0C);
  const uint8_t fc3[] = {3, 0, 10, 0, 3};
  CHECK(mb.answer(fc3, 5, o) == 8 && o[0] == 3);
  const uint8_t status[] = {4, 0, 1, 0, 3};
  CHECK(mb.answer(status, 5, o) == 8 && o[3] == 1 && o[6] == 0 && o[7] == 71);
  r.cmd("r21");
  const uint8_t bits[] = {1, 0, 0, 0, 2};
  CHECK(mb.answer(bits, 5, o) == 3 && o[1] == 1 && o[2] == 2);
  const uint8_t bits2[] = {2, 0, 1, 0, 1};
  CHECK(mb.answer(bits2, 5, o) == 3 && o[0] == 2 && o[2] == 1);
  const uint8_t bad[] = {1, 0, 2, 0, 1};
  CHECK(mb.answer(bad, 5, o) == 2 && o[0] == 0x81 && o[1] == 2);
  const uint8_t gap[] = {4, 1, 0, 0, 1};
  CHECK(mb.answer(gap, 5, o) == 2 && o[0] == 0x84 && o[1] == 2);
  const uint8_t none[] = {4, 0, 10, 0, 0};
  CHECK(mb.answer(none, 5, o) == 2 && o[1] == 3);
  const uint8_t tooMany[] = {4, 0, 10, 0, 126};
  CHECK(mb.answer(tooMany, 5, o) == 2 && o[1] == 3);
  const uint8_t write1[] = {6, 0, 10, 0, 1};
  CHECK(mb.answer(write1, 5, o) == 2 && o[0] == 0x86 && o[1] == 1);
  const uint8_t writeCoil[] = {5, 0, 0, 0xFF, 0};
  CHECK(mb.answer(writeCoil, 5, o) == 2 && o[0] == 0x85 && o[1] == 1);
  CHECK(!r.b.r1->isOn());
}

static void pzemFrame() {
  uint8_t q[8];
  dpzem::makeRequest(q);
  CHECK(q[0] == 0xF8 && q[1] == 0x04 && q[5] == 10);
  CHECK(q[6] == 0x64 && q[7] == 0x64);
  uint8_t a[25] = {0xF8, 0x04, 20, 0x08, 0xFC,  // 230.0 V
                   0x03, 0xE8, 0x00, 0x00,      // 1.000 A
                   0x08, 0xFC, 0x00, 0x00,      // 230.0 W
                   0x00, 0x01, 0x00, 0x01,      // 65.537 kWh
                   0x01, 0xF4, 0x00, 0x64, 0, 0};
  uint16_t c = dCrc16(a, 23);
  a[23] = (uint8_t)c;
  a[24] = (uint8_t)(c >> 8);
  dpzem::Reading x;
  CHECK(dpzem::parse(a, 25, x));
  CHECK(x.voltage > 229.9f && x.voltage < 230.1f && x.current > 0.99f && x.current < 1.01f);
  CHECK(x.power > 229.9f && x.power < 230.1f && x.energy > 65.5f && x.energy < 65.6f);
  a[4] ^= 1;
  CHECK(!dpzem::parse(a, 25, x));
  CHECK(!dpzem::parse(a, 24, x));
}

struct FakeButton : DButton {
  bool down = false;
  using DButton::DButton;
  bool pressedNow() override { return down; }
};

static DButton::Press hold(Bed& e, FakeButton& b, uint32_t ms) {
  b.down = true;
  b.loop();
  e.clock.now += 10;
  b.loop();
  e.clock.now += ms;
  b.down = false;
  b.loop();
  e.clock.now += 10;
  DButton::Press p = b.loop();
  e.clock.now += 100;
  b.loop();
  return p;
}

static void rtuAndJsy() {
  uint8_t q[8];
  drtu::makeRead(1, 3, 72, 10, q);
  const uint8_t want[] = {0x01, 0x03, 0x00, 0x48, 0x00, 0x0A};
  CHECK(memcmp(q, want, 6) == 0);
  CHECK(dCrc16(q, 8) == 0);  // a frame with its CRC gives 0
  // an answer: 230.12 V, 1.234 A, 345.6 W, 32000 pulses = 10 kWh, pf 0.950, 50.01 Hz
  uint16_t regs[10] = {23012, 1234, 3456, 0, 32000, 950, 0, 0, 25, 5001};
  uint8_t a[25] = {0x01, 0x03, 20};
  for (int i = 0; i < 10; i++) {
    a[3 + 2 * i] = regs[i] >> 8;
    a[4 + 2 * i] = regs[i] & 0xFF;
  }
  uint16_t c = dCrc16(a, 23);
  a[23] = c & 0xFF;
  a[24] = c >> 8;
  uint16_t got[10];
  CHECK(drtu::parseRead(a, 25, 1, 3, 10, got) && got[9] == 5001);
  djsy::Reading x;
  djsy::decode(got, x);
  CHECK(x.voltage100 == 23012 && x.current1000 == 1234 && x.power10 == 3456 && x.pf1000 == 950 && x.freq100 == 5001);
  CHECK(x.energy > 9.999f && x.energy < 10.001f);
  got[3] = 1;  // the high word of the energy
  djsy::decode(got, x);
  CHECK(x.energy > 30.47f && x.energy < 30.49f);  // (65536 + 32000) / 3200
  CHECK(!drtu::parseRead(a, 25, 2, 3, 10, got));  // another address
  CHECK(!drtu::parseRead(a, 24, 1, 3, 10, got));
  a[10] ^= 1;
  CHECK(!drtu::parseRead(a, 25, 1, 3, 10, got));  // CRC
  const uint8_t ex[] = {0x01, 0x83, 0x02, 0xC0, 0xF1};  // an exception answer
  CHECK(!drtu::parseRead(ex, 5, 1, 3, 10, got));
}

static void buttonPresses() {
  Bed e;
  FakeButton b(e.ctx, "b");
  CHECK(hold(e, b, 100) == DButton::Press::Short);
  CHECK(hold(e, b, 3000) == DButton::Press::Config);
  CHECK(hold(e, b, 7000) == DButton::Press::None);
  CHECK(hold(e, b, 9000) == DButton::Press::Reset);
  CHECK(hold(e, b, 5) == DButton::Press::None);
}

struct FakeLed : DLed {
  std::string out;
  using DLed::DLed;
  void write(bool h) override { out += h ? '1' : '0'; }
};

static void ledPattern() {
  Bed e;
  FakeLed l(e.ctx, "l");
  l.setPattern(DLed::Offline);
  for (int i = 1; i <= 8; i++) {
    e.clock.now = 250 * i;
    l.loop();
  }
  CHECK(l.out == "10100000");
}

static void disabledRelayAndExtras() {
  Rig r;
  r.cmd("sh r9");
  CHECK(r.ev.lastInfo() == "I:R9=DISABLED");
  r.cmd("r91");
  CHECK(r.ev.lastInfo() == "I:R9=DISABLED");
  r.cmd("sh r1");
  CHECK(r.ev.lastInfo() == "I:R1=OFF");
  r.cmd("r11");
  r.cmd("sh r1");
  CHECK(r.ev.lastInfo() == "I:R1=ON");
  r.cmd("sh net");
  CHECK(r.ev.lastInfo() == "E: sh param not recognized");
  struct X : DCmdExtra {
    bool extraShow(const std::string& w, std::string& o) override {
      if (w != "net") return false;
      o = "SSID=x";
      return true;
    }
  } x;
  r.b.commands().setExtra(&x);
  r.cmd("sh net");
  CHECK(r.ev.lastInfo() == "SSID=x");
  r.cmd("devtype=pump");
  CHECK(std::string(r.b.type()) == "pump");
  r.cmd("mbtcp=1;");
  CHECK(r.s.mbtcp == 1 && r.ctx.saveWanted);
  CHECK(r.b.commands().saveText().find("devType=pump") != std::string::npos);
}

static void defaultResetsAutomation() {
  Rig r;
  r.cmd("hotter=1");
  r.cmd("lschm2=16");
  r.cmd("aofs2=60");
  r.cmd("ctempl=25");
  r.cmd("tmatrix=5:30");
  r.cmd("cschm1=3:1");
  r.cmd("clevel1=150");
  r.cmd("tz=5");
  r.cmd("mbtcp=1");
  r.cmd("devtype=pump");
  r.cmd("r11");
  r.cmd("r21");
  r.ctx.saveWanted = false;
  r.cmd("default=2");
  CHECK(r.ev.lastInfo() == "E:params error" && r.s.hotter == 1);
  r.cmd("default=1;");
  const DSettings d;
  CHECK(r.ev.lastInfo() == "N:default=1, automation reset" && r.ctx.saveWanted);
  CHECK(r.s.hotter == 0 && r.s.lschm2 == 0 && r.s.aofs2 == 0 && r.s.ctempl == d.ctempl);
  CHECK(r.s.tmatrix[5] == d.tmatrix[5] && r.s.cschm1 == d.cschm1 && r.s.clevel[0] == -128);
  CHECK(!r.b.r1->isOn() && !r.b.r2->isOn());
  CHECK(r.s.tz == 5 && r.s.mbtcp == 1 && std::string(r.s.devType) == "pump");  // not automation: kept
}

static void i2cPinSettings() {
  Rig r;
  struct X : DCmdExtra {
    bool extraShow(const std::string&, std::string&) override { return false; }
    bool pinBusy(long p, const std::string& n) override { return p == 5 || p > 15 || (n == "i2cscl" && p == 4); }
  } x;
  r.b.commands().setExtra(&x);
  CHECK(r.s.i2csda == -1 && r.s.i2cscl == -1);
  r.cmd("i2cscl=12;");
  CHECK(r.s.i2cscl == 12 && r.ev.lastInfo() == "N:i2cscl=12, after reset" && r.ctx.saveWanted);
  r.cmd("i2csda=5");
  CHECK(r.s.i2csda == -1 && r.ev.lastInfo() == "E:pin busy");
  r.cmd("i2cscl=4");  // the pin sda is on now
  CHECK(r.s.i2cscl == 12 && r.ev.lastInfo() == "E:pin busy");
  r.cmd("i2csda=12");
  CHECK(r.s.i2csda == -1 && r.ev.lastInfo() == "E:pin busy");  // the same pin as scl
  r.cmd("i2csda=40");
  CHECK(r.s.i2csda == -1 && r.ev.lastInfo() == "E:params error");
  r.cmd("i2csda=4");
  CHECK(r.s.i2csda == 4);
  r.cmd("i2cscl=-1");
  CHECK(r.s.i2cscl == -1);
  r.cmd("sh i2csda");
  CHECK(r.ev.lastInfo() == "i2csda=4");
  CHECK(r.b.commands().saveText().find("i2csda=4\ni2cscl=-1\n") != std::string::npos);
  r.cmd("rturx=4");  // i2csda has it
  CHECK(r.s.rturx == -1 && r.ev.lastInfo() == "E:pin busy");
  r.cmd("rturx=12;");
  CHECK(r.s.rturx == 12 && r.ev.lastInfo() == "N:rturx=12, after reset");
  r.cmd("rtutx=12");
  CHECK(r.s.rtutx == -1 && r.ev.lastInfo() == "E:pin busy");
  r.cmd("rtutx=13");
  CHECK(r.s.rtutx == 13);
}

int main() {
  valueFormat();
  multiValueSensor();
  sensorPeriodAndRequest();
  hotterHysteresis();
  coolerHysteresis();
  sensorLostTurnsOff();
  levelUnits();
  dayNightAndMatrix();
  noTimeUsesDefaultLevel();
  scheduleByHour();
  wateringOncePerHour();
  noTimeSchedules();
  staleTimeKeepsSchedules();
  autoStopManual();
  commandsAnswerLikeOld();
  schemesRefused();
  modesExclude();
  savingRules();
  antiFlood();
  customBits();
  shAllGoesToLogOnly();
  limiterTripsAndSaves();
  limiterResetsBelowThreshold();
  limiterOffWhenNoThreshold();
  legacySettings();
  legacyRealSample();
  settingsSurviveRestart();
  medianFilter();
  differenceOfTwo();
  modbusAnswers();
  pzemFrame();
  rtuAndJsy();
  buttonPresses();
  ledPattern();
  disabledRelayAndExtras();
  i2cPinSettings();
  defaultResetsAutomation();
  printf("%d checks, %d failed\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
