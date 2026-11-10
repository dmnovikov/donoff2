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
      ctx_.saveWanted = false;
      if (storage_) {
        bool ok = storage_->save(commands_.saveText());
        ctx_.ev.log(ok ? "N:settings saved" : "E:settings not saved");
      }
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
  std::vector<DSensor*>& sensors() { return sensors_; }
  std::vector<DRelay*>& relays() { return relays_; }
  const char* type() const { return name_; }

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
