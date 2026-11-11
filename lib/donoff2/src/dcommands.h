// The commands of donoff, compatible with donoff-lib. One table gives set, sh and sh all.
#pragma once
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "drelay.h"
#include "drule.h"
#include "dsensor.h"

// What only the board can answer: sh net, sh ip, sh pinset ...
class DCmdExtra {
 public:
  virtual ~DCmdExtra() = default;
  virtual bool extraShow(const std::string& what, std::string& out) = 0;
  // taken on this board (also by a bus line in use), or not usable at all; name is the pin setting being changed
  virtual bool pinBusy(long pin, const std::string& name) { return false; }
};

class DCommands : public DBase {
 public:
  enum Kind : uint8_t { Plain, Scheme, Hotter, Cooler, Lschm1, RelayScheme, Pin };
  struct Param {
    const char* name;
    const char* a1;
    const char* a2;
    int32_t* v;
    int32_t lo, hi;
    Kind kind;
    int8_t relay = -1;   // RelayScheme: the relay that goes off when the scheme is set to 0
    bool portal = false; // also a field of the setup portal
    bool is(const std::string& n) const { return n == name || (a1 && n == a1) || (a2 && n == a2); }
  };

  static constexpr uint32_t kFloodMs = 1000;

  DCommands(DContext& ctx, std::vector<DRelay*>& relays, std::vector<DSensor*>& sensors)
      : DBase(ctx, "commands"), relays_(relays), sensors_(sensors) {
    DSettings& s = ctx_.s;
    table_ = {
        {"aofs", "aofs1", nullptr, &s.aofs1, 0, 1200, Plain},
        {"aofs2", "a2ofs", nullptr, &s.aofs2, 0, 1200, Plain},
        {"lschm", "lschm1", "schm1", &s.lschm1, 0, 100, Lschm1},
        {"lschm2", "l2schm", "schm2", &s.lschm2, 0, 100, RelayScheme, 1},
        {"nosync1", nullptr, nullptr, &s.nosync1, 0, 1, Plain},
        {"nosync2", nullptr, nullptr, &s.nosync2, 0, 1, Plain},
        {"hotter", "h", nullptr, &s.hotter, 0, 255, Hotter},
        {"cooler", nullptr, nullptr, &s.cooler, 0, 1, Cooler},
        {"ctempl", nullptr, nullptr, &s.ctempl, -5000, 10000, Plain},
        {"dtempl", nullptr, nullptr, &s.dtempl, -5000, 10000, Plain},
        {"ntempl", nullptr, nullptr, &s.ntempl, -5000, 10000, Plain},
        {"tdelta", nullptr, nullptr, &s.tdelta, 0, 50000, Plain},
        {"onoffschm1", nullptr, nullptr, &s.onoffschm, 0, 100, Scheme},
        {"tz", nullptr, nullptr, &s.tz, -15, 15, Plain},
        {"autoreboot", nullptr, nullptr, &s.autoreboot, 0, 1, Plain},
        {"clevel1", nullptr, nullptr, &s.clevel[0], -32000, 32000, Plain},
        {"clevel2", nullptr, nullptr, &s.clevel[1], -32000, 32000, Plain},
        {"clevel3", nullptr, nullptr, &s.clevel[2], -32000, 32000, Plain},
        {"clevel4", nullptr, nullptr, &s.clevel[3], -32000, 32000, Plain},
        {"mbtcp", nullptr, nullptr, &s.mbtcp, 0, 1, Plain, -1, true},
        {"mbport", nullptr, nullptr, &s.mbport, 1, 65535, Plain, -1, true},
        {"mbunit", nullptr, nullptr, &s.mbunit, 1, 247, Plain, -1, true},
        {"rtubaud", nullptr, nullptr, &s.rtubaud, 1200, 115200, Plain},
        {"rtuparity", nullptr, nullptr, &s.rtuparity, 0, 2, Plain},
        {"rtustop", nullptr, nullptr, &s.rtustop, 1, 2, Plain},
        {"rtuaddr", nullptr, nullptr, &s.rtuaddr, 1, 247, Plain},
        {"i2csda", nullptr, nullptr, &s.i2csda, -1, 39, Pin},
        {"i2cscl", nullptr, nullptr, &s.i2cscl, -1, 39, Pin},
        {"rturx", nullptr, nullptr, &s.rturx, -1, 39, Pin},
        {"rtutx", nullptr, nullptr, &s.rtutx, -1, 39, Pin},
    };
  }

  void addParam(const Param& p) { table_.push_back(p); }
  void setExtra(DCmdExtra* e) { extra_ = e; }
  std::vector<Param>& params() { return table_; }

  // The settings as text, "name=value" per line: a new parameter moves nothing in the file.
  std::string saveText() const {
    const DSettings& s = ctx_.s;
    std::string t;
    for (auto& p : table_) t += std::string(p.name) + "=" + std::to_string(*p.v) + "\n";
    t += "cschm1=" + std::to_string(s.cschm1) + "\ncschm2=" + std::to_string(s.cschm2) + "\ntmatrix=";
    for (int i = 0; i < 24; i++) t += (i ? "," : "") + std::to_string(s.tmatrix[i]);
    t += "\n";
    t += std::string("mqttServer=") + s.mqttServer + "\nmqttUser=" + s.mqttUser + "\nmqttPass=" + s.mqttPass + "\nmqttPort=" + s.mqttPort + "\ndevId=" + s.devId + "\nemail=" + s.email + "\ndevType=" + s.devType + "\n";
    return t;
  }

  // Back from the text, no side effects. Unknown lines and values out of range are skipped.
  void loadText(const char* text) {
    std::string t(text);
    size_t pos = 0;
    while (pos < t.size()) {
      size_t nl = t.find('\n', pos);
      if (nl == std::string::npos) nl = t.size();
      std::string line = t.substr(pos, nl - pos);
      pos = nl + 1;
      size_t eq = line.find('=');
      if (eq != std::string::npos) loadLine(line.substr(0, eq), line.substr(eq + 1));
    }
  }

  // One message of /in/params.
  void handle(const char* text) {
    std::string in(text);
    while (!in.empty() && isSpace(in.back())) in.pop_back();
    size_t b = 0;
    while (b < in.size() && isSpace(in[b])) b++;
    in.erase(0, b);
    bool save = !in.empty() && in.back() == ';';
    if (save) in.pop_back();
    if (in.empty()) return;

    if (in.rfind("sh ", 0) == 0) {  // a question moves nothing: no anti-flood
      if (!show(in.substr(3))) ctx_.ev.info("E: sh param not recognized");
      return;
    }
    uint32_t now = ctx_.clock.ms();
    if (commanded_ && now - lastCmd_ < kFloodMs) {
      ctx_.ev.info("DDOS CHECK");
      return;
    }
    commanded_ = true;
    lastCmd_ = now;

    size_t eq = in.find('=');
    if (eq != std::string::npos) {
      if (set(in.substr(0, eq), in.substr(eq + 1)) && save) ctx_.saveWanted = true;
      return;
    }
    if (!bare(in)) ctx_.ev.info("no command");
  }

 private:
  void loadLine(const std::string& k, const std::string& v) {
    DSettings& s = ctx_.s;
    long n;
    for (auto& p : table_)
      if (k == p.name) {
        if (toInt(v, &n) && n >= p.lo && n <= p.hi) *p.v = n;
        return;
      }
    if (k == "cschm1" && toInt(v, &n)) s.cschm1 = (uint32_t)n & 0xFFFFFFul;
    else if (k == "cschm2" && toInt(v, &n)) s.cschm2 = (uint32_t)n & 0xFFFFFFul;
    else if (k == "tmatrix") loadMatrix(v);
    else if (k == "mqttServer") copy(s.mqttServer, v);
    else if (k == "mqttUser") copy(s.mqttUser, v);
    else if (k == "mqttPass") copy(s.mqttPass, v);
    else if (k == "mqttPort") copy(s.mqttPort, v);
    else if (k == "devId") copy(s.devId, v);
    else if (k == "email") copy(s.email, v);
    else if (k == "devType") copy(s.devType, v);
  }

  void loadMatrix(const std::string& v) {
    size_t pos = 0;
    for (int i = 0; i < 24 && pos <= v.size(); i++) {
      size_t c = v.find(',', pos);
      if (c == std::string::npos) c = v.size();
      long n;
      if (toInt(v.substr(pos, c - pos), &n) && n >= -5000 && n <= 10000) ctx_.s.tmatrix[i] = n;
      pos = c + 1;
    }
  }

  template <size_t N>
  static void copy(char (&to)[N], const std::string& v) {
    if (v.size() >= N) return;
    strcpy(to, v.c_str());
  }

  static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

  static bool toInt(const std::string& s, long* out) {
    if (s.empty()) return false;
    char* end;
    errno = 0;
    *out = strtol(s.c_str(), &end, 10);
    return *end == 0 && errno == 0;
  }

  void relayOff(size_t i, const char* why) {
    if (i < relays_.size()) relays_[i]->off(why);
  }

  void say(const std::string& s) { ctx_.ev.info(s.c_str()); }

  bool set(const std::string& name, const std::string& val) {
    for (auto& p : table_)
      if (p.is(name)) return setParam(p, name, val);
    if (name == "tmatrix") return setMatrix(val);
    if (name == "cschm1") return setCustom(ctx_.s.cschm1, "N:CCSCM1", val);
    if (name == "cschm2") return setCustom(ctx_.s.cschm2, "N:CCSCM2", val);
    if (name == "om1") return setBits(ctx_.s.cschm1, val);
    if (name == "om2") return setBits(ctx_.s.cschm2, val);
    if (name == "email") return setText(ctx_.s.email, sizeof ctx_.s.email, "email", val);
    if (name == "devtype") return setText(ctx_.s.devType, sizeof ctx_.s.devType, "devtype", val);
    if (name == "default") return setDefault(val);
    ctx_.ev.info("E:Varable error");
    return false;
  }

  // default=1: every automation back to the defaults, all relays off. The connection, the name, tz,
  // autoreboot, Modbus and the I2C pins stay.
  bool setDefault(const std::string& val) {
    if (val != "1") {
      ctx_.ev.info("E:params error");
      return false;
    }
    DSettings& s = ctx_.s;
    const DSettings d;
    s.aofs1 = d.aofs1;
    s.aofs2 = d.aofs2;
    s.lschm1 = d.lschm1;
    s.lschm2 = d.lschm2;
    s.nosync1 = d.nosync1;
    s.nosync2 = d.nosync2;
    s.nosync3 = d.nosync3;
    s.nosync4 = d.nosync4;
    s.hotter = d.hotter;
    s.cooler = d.cooler;
    s.ctempl = d.ctempl;
    s.dtempl = d.dtempl;
    s.ntempl = d.ntempl;
    s.tdelta = d.tdelta;
    s.onoffschm = d.onoffschm;
    s.cschm1 = d.cschm1;
    s.cschm2 = d.cschm2;
    for (int i = 0; i < 4; i++) s.clevel[i] = d.clevel[i];
    for (int i = 0; i < 24; i++) s.tmatrix[i] = d.tmatrix[i];
    for (auto* r : relays_) r->off("default");
    say("N:default=1, automation reset");
    return true;
  }

  bool setParam(Param& p, const std::string& name, const std::string& val) {
    long v;
    if (!toInt(val, &v)) {
      ctx_.ev.info(p.hi == 1 ? "E:not set, 01 wait" : "E:not set, INT wait");
      return false;
    }
    if (v < p.lo || v > p.hi || ((p.kind == Lschm1 || p.kind == RelayScheme || p.kind == Scheme) && !dscheme::valid(v))) {
      ctx_.ev.info("E:params error");
      return false;
    }
    if (p.kind == Pin && v >= 0) {
      bool taken = false;  // another pin setting has it
      for (auto& q : table_)
        if (q.kind == Pin && q.v != p.v && *q.v == v) taken = true;
      if (taken || (extra_ && extra_->pinBusy(v, p.name))) {
        ctx_.ev.info("E:pin busy");
        return false;
      }
    }
    *p.v = v;
    apply(p, v);
    say("N:" + name + "=" + std::to_string(v) + (p.kind == Pin ? ", after reset" : ""));
    return true;
  }

  void apply(const Param& p, long v) {
    DSettings& s = ctx_.s;
    switch (p.kind) {
      case Hotter:
        if (v == 1 || v == 2) {
          s.lschm1 = s.aofs1 = s.cooler = 0;
        } else if (v == 0) {
          relayOff(0, "from publisher");
        }
        break;
      case Cooler:
        if (v) s.lschm1 = s.aofs1 = s.hotter = 0;
        else relayOff(0, "from publisher");
        break;
      case Lschm1:
        if (v > 0) s.hotter = s.cooler = 0;
        else relayOff(0, "lschm or hotter=0");
        break;
      case RelayScheme:
        if (v == 0) relayOff(p.relay, "lschm or hotter=0");
        break;
      default:
        break;
    }
  }

  // tmatrix=22 (all hours) or tmatrix=<hour>:<level>
  bool setMatrix(const std::string& val) {
    size_t c = val.find(':');
    long h = -1, v;
    bool ok = c == std::string::npos ? toInt(val, &v) : (toInt(val.substr(0, c), &h) && toInt(val.substr(c + 1), &v) && h >= 0 && h <= 23);
    if (!ok || v < -5000 || v > 10000) {
      ctx_.ev.info("E:params error");
      return false;
    }
    if (c == std::string::npos) {
      for (auto& x : ctx_.s.tmatrix) x = v;
      say("N: ALLMATRIX=" + std::to_string(v));
    } else {
      ctx_.s.tmatrix[h] = v;
      say("N:V[" + std::to_string(h) + "]=" + std::to_string(v));
    }
    return true;
  }

  // cschm1=0, cschm1=1, cschm1=<hour>:<0|1>
  bool setCustom(uint32_t& bits, const char* tag, const std::string& val) {
    size_t c = val.find(':');
    if (c == std::string::npos && (val == "0" || val == "1")) {
      bits = val == "1" ? 0xFFFFFFul : 0ul;
      say(std::string(tag) + "=" + val);
      return true;
    }
    long h, v;
    if (c == std::string::npos || !toInt(val.substr(0, c), &h) || !toInt(val.substr(c + 1), &v) || h < 0 || h > 23 || v < 0 || v > 1) {
      ctx_.ev.info("E:params error");
      return false;
    }
    if (v) bits |= 1ul << (23 - h);
    else bits &= ~(1ul << (23 - h));
    say("N:V[" + std::to_string(h) + "]=" + std::to_string(v));
    return true;
  }

  bool setBits(uint32_t& bits, const std::string& val) {
    if (val.size() != 24 || val.find_first_not_of("01") != std::string::npos) {
      ctx_.ev.info("E:Var error");
      return false;
    }
    uint32_t b = 0;
    for (char c : val) b = (b << 1) | (c == '1');
    bits = b;
    ctx_.ev.info("N:ONOFFMATRIX saved");
    return true;
  }

  bool setText(char* to, size_t n, const char* name, const std::string& val) {
    if (val.size() >= n) {
      ctx_.ev.info("E:params error");
      return false;
    }
    strcpy(to, val.c_str());
    say(std::string("N:") + name + "=" + val);
    return true;
  }

  static std::string bitsText(uint32_t bits) {
    std::string s;
    for (int i = 0; i < 24; i++) {
      if (i && i % 4 == 0) s += ' ';
      s += ((bits >> (23 - i)) & 1) ? '1' : '0';
    }
    return s;
  }

  std::string levelsText() const {
    const DSettings& s = ctx_.s;
    return "C=" + std::to_string(s.ctempl) + ",D=" + std::to_string(s.dtempl) + ",N=" + std::to_string(s.ntempl) + " dT=" + std::to_string(s.tdelta);
  }

  bool show(const std::string& what) {
    DSettings& s = ctx_.s;
    for (auto& p : table_)
      if (p.is(what)) {
        say(what + "=" + std::to_string(*p.v));
        return true;
      }
    if (what == "tls" || what == "tlevels") say(levelsText());
    else if (what == "time")
      say("I:S=" + std::to_string(ctx_.clock.timeState() == DTimeState::Synced) + ",T=" + std::to_string(ctx_.clock.hour()) + ":" + std::to_string(ctx_.clock.minute()) + ":" + std::to_string(ctx_.clock.year()));
    else if (what == "schma" || what == "schma1") say(what + "=" + std::to_string(s.lschm1) + ":" + std::to_string(s.aofs1));
    else if (what == "schma2") say(what + "=" + std::to_string(s.lschm2) + ":" + std::to_string(s.aofs2));
    else if (what == "cschm1") say(bitsText(s.cschm1));
    else if (what == "cschm2") say(bitsText(s.cschm2));
    else if (what == "email") say(std::string("email=") + s.email);
    else if (what == "devtype") say(std::string("devtype=") + s.devType);
    else if (what == "salt") say("salt=1206");
    else if (what.rfind("bschm", 0) == 0) ctx_.ev.info("depricated!");
    else if (what.size() == 2 && what[0] == 'r' && what[1] >= '1' && what[1] <= '9') {
      size_t i = what[1] - '1';
      std::string tag = "I:R" + std::string(1, what[1]) + "=";
      say(tag + (i >= relays_.size() ? "DISABLED" : relays_[i]->isOn() ? "ON" : "OFF"));
    }
    else if (what == "all") ctx_.ev.log(allJson().c_str());
    else {
      std::string out;
      if (!extra_ || !extra_->extraShow(what, out)) return false;
      if (!out.empty()) say(out);
    }
    return true;
  }

  // sh all: everything in one line, to the log only.
  std::string allJson() {
    std::string j = "{\"synced\":" + std::to_string(ctx_.clock.timeState() == DTimeState::Synced);
    for (auto& p : table_) j += std::string(",\"") + p.name + "\":" + std::to_string(*p.v);
    j += ",\"cschm1\":" + std::to_string(ctx_.s.cschm1) + ",\"cschm2\":" + std::to_string(ctx_.s.cschm2);
    for (size_t i = 0; i < relays_.size(); i++) j += std::string(",\"") + relays_[i]->name() + "\":" + std::to_string(relays_[i]->isOn());
    char buf[24];
    for (auto* sn : sensors_)
      for (size_t i = 0; i < sn->count(); i++) {
        sn->value(i).format(buf, sizeof buf);
        j += std::string(",\"") + sn->value(i).name + "\":" + buf;
      }
    return j + "}";
  }

  bool bare(const std::string& in) {
    if (in == "save") {
      ctx_.saveWanted = true;
      return true;
    }
    if (in == "reset") {
      ctx_.ev.info("I: OK, LET'S RESET");
      ctx_.rebootWanted = true;
      return true;
    }
    if (in == "resethour1" || in == "resethour2") {
      size_t i = in.back() - '1';
      std::string tag = std::string("I:R") + in.back();
      if (i >= relays_.size()) {
        say(tag + "=DISABLED");
        return true;
      }
      relays_[i]->resetLastHourOn();
      say(std::string("I: R") + in.back() + " lschm last hour is reseted");
      return true;
    }
    if (in.size() == 3 && in[0] == 'r' && in[1] >= '1' && in[1] <= '9' && (in[2] == '0' || in[2] == '1')) {
      size_t i = in[1] - '1';
      if (i >= relays_.size()) {
        say(std::string("I:R") + in[1] + "=DISABLED");
        return true;
      }
      if (in[2] == '1') relays_[i]->on("from publisher");
      else relays_[i]->off("from publisher");
      return true;
    }
    return false;
  }

  std::vector<DRelay*>& relays_;
  std::vector<DSensor*>& sensors_;
  std::vector<Param> table_;
  DCmdExtra* extra_ = nullptr;
  uint32_t lastCmd_ = 0;
  bool commanded_ = false;
};
