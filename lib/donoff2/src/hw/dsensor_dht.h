// DHT22 or DHT11 (type DHT22 / DHT11 of the Adafruit library). Temperature and humidity are two lines of one table and one JSON (dht_json).
#pragma once
#include <DHT.h>
#include "../dsensor.h"

class DSensorDHT : public DSensor {
 public:
  DSensorDHT(DContext& ctx, uint8_t pin, uint16_t mbReg, uint8_t type = DHT22)
      : DSensor(ctx, "dht", 5000, 0, 10000), dht_(pin, type) {
    value(addValue("dht_humidity", 100, "%", mbReg, 6)).key = "humidity";
    value(addValue("dht_temp", 100, "C", mbReg + 3, 8)).key = "temp";
    value(0).label = "DHT_HUMIDITY";
    value(1).label = "DHT_TEMP";
    setGroup("dht_json");
  }

 protected:
  void onBegin() override { dht_.begin(); }
  bool collect() override {
    float h = dht_.readHumidity(), t = dht_.readTemperature();
    if (isnan(h) || isnan(t)) return false;
    value(0).set(h);
    value(1).set(t);
    return true;
  }

 private:
  DHT dht_;
};
