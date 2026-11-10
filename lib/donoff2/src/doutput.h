// A way out of the device: MQTT today, Modbus next. Reads the tables, knows no sensor types.
#pragma once
#include "ddevice.h"

class DOutput : public DBase {
 public:
  DOutput(DContext& ctx, const char* name, DDevice& dev) : DBase(ctx, name), dev_(dev) {}
  virtual void begin() {}
  virtual void loop() = 0;

 protected:
  DDevice& dev_;
};
