// DDevice: a board. A concrete class only says what is on it; the work is the same for all.
#pragma once
#include <vector>
#include "dcommands.h"
#include "drelay.h"
#include "drule.h"
#include "dsensor.h"
#include "dstorage.h"

class DDevice : public DBase {
 public:
  DDevice(DContext& ctx, const char* type) : DBase(ctx, type), commands_(ctx, relays_, sensors_) {}

  void begin() {
    build();
    for (auto* s : sensors_) s->begin();
  }

  void loop() {
    for (auto* s : sensors_) s->loop();
    uint32_t now = ctx_.clock.ms();
    if (ruled_ && now - lastRules_ < kRulesMs) return;
    ruled_ = true;
    lastRules_ = now;
    for (auto* r : rules_) r->loop();
    if (ctx_.saveWanted) {
      saveNow();
    }
  }

  // The settings from the storage; false: nothing is stored yet.
  bool loadSettings() {
    std::string t;
    if (!storage_ || !storage_->load(t)) return false;
    commands_.loadText(t.c_str());
    return true;
  }
  void setStorage(DStorage* s) { storage_ = s; }
  void saveNow() {
    ctx_.saveWanted = false;
    if (storage_) ctx_.ev.info(storage_->save(commands_.saveText()) ? "N: saved" : "E: not saved");
  }
  virtual void shortPress() {}  // the button; what it does depends on the board
  std::vector<DSensor*>& sensors() { return sensors_; }
  std::vector<DRelay*>& relays() { return relays_; }
  const char* type() const { return ctx_.s.devType[0] ? ctx_.s.devType : name_; }
  virtual bool hasRs485() const { return false; }  // the portal shows the RTU fields

  DCommands& commands() { return commands_; }
  DValue* findValue(const char* n) {
    for (auto* s : sensors_)
      if (DValue* v = s->find(n)) return v;
    return nullptr;
  }

 protected:
  static constexpr uint32_t kRulesMs = 1000;
  virtual void build() = 0;  // creates the sensors, the relays and the rules

  std::vector<DSensor*> sensors_;
  std::vector<DRelay*> relays_;
  std::vector<DRule*> rules_;

 private:
  DCommands commands_;
  DStorage* storage_ = nullptr;
  uint32_t lastRules_ = 0;
  bool ruled_ = false;
};
