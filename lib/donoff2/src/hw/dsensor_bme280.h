// BME280: temperature, humidity and pressure, one JSON (bme_json). SDO low gives 0x76, high gives 0x77.
// BMP280 (id 0x58) is the same chip without humidity: it is read too, bme_humidity stays -127.
// The chip measures by itself (normal mode, 1 s standby); compensation is the integer one of the datasheet.
#pragma once
#include "dsensor_i2c.h"

class DSensorBME280 : public DSensorI2C {
 public:
  DSensorBME280(DContext& ctx, uint8_t sda, uint8_t scl, uint16_t mbReg)
      : DSensorI2C(ctx, "bme", 5000, 0, 0, sda, scl, 0x76, 0x77) {
    value(addValue("bme_temp", 100, "C", mbReg, 8)).key = "temp";
    value(addValue("bme_humidity", 100, "%", mbReg + 3, 6)).key = "humidity";
    value(addValue("bme_pressure", 100, "hPa", mbReg + 6, 11)).key = "pressure";
    value(0).label = "BME_TEMP";
    value(1).label = "BME_HUMIDITY";
    value(2).label = "BME_PRESSURE";
    setGroup("bme_json");
  }

 protected:
  uint8_t setupChip() override {
    uint8_t id = 0, c[26], h[7] = {};
    if (!readRegs(0xD0, &id, 1)) return kReadFailed;
    if (id != 0x60 && id != 0x58) return kWrongChip;
    bool hum = id == 0x60;
    if (hum != hum_ || !told_) ctx_.ev.log(hum ? "I:bme is BME280" : "I:bme is BMP280, no humidity");
    told_ = true;
    hum_ = hum;
    if (!readRegs(0x88, c, 26) || (hum_ && !readRegs(0xE1, h, 7))) return kReadFailed;
    t1_ = u16(c, 0);
    t2_ = (int16_t)u16(c, 2);
    t3_ = (int16_t)u16(c, 4);
    p1_ = u16(c, 6);
    for (uint8_t i = 0; i < 8; i++) p_[i] = (int16_t)u16(c, 8 + 2 * i);  // P2..P9
    h1_ = c[25];
    h2_ = (int16_t)u16(h, 0);
    h3_ = h[2];
    h4_ = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F));
    h5_ = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));
    h6_ = (int8_t)h[6];
    uint8_t e = hum_ ? writeReg(0xF2, 0x01) : 0;  // humidity x1 (takes effect with the next write)
    if (!e) e = writeReg(0xF5, 0xA0);   // standby 1 s, no filter
    if (!e) e = writeReg(0xF4, 0x27);   // temperature x1, pressure x1, normal mode
    return e;
  }

  bool measure() override {
    uint8_t b[8] = {};
    if (!readRegs(0xF7, b, hum_ ? 8 : 6)) return false;
    int32_t adcP = ((uint32_t)b[0] << 12) | ((uint32_t)b[1] << 4) | (b[2] >> 4);
    int32_t adcT = ((uint32_t)b[3] << 12) | ((uint32_t)b[4] << 4) | (b[5] >> 4);
    int32_t adcH = ((uint32_t)b[6] << 8) | b[7];
    if (adcT == 0x80000) {  // nothing measured yet
      for (size_t i = 0; i < count(); i++) value(i).notReady();
      return true;
    }
    int32_t tFine;
    value(0).setRaw(temperature(adcT, tFine));             // 0.01 C
    if (hum_) value(1).setRaw((long)((humidity(adcH, tFine) * 100) >> 10));  // %RH * 1024 -> 0.01 %
    else value(1).fail();
    value(2).setRaw((long)(pressure(adcP, tFine) >> 8));  // Pa * 256 -> Pa = 0.01 hPa
    return true;
  }

 private:
  static uint16_t u16(const uint8_t* b, uint8_t i) { return b[i] | (b[i + 1] << 8); }

  int32_t temperature(int32_t adc, int32_t& tFine) const {
    int32_t v1 = ((((adc >> 3) - ((int32_t)t1_ << 1))) * t2_) >> 11;
    int32_t d = (adc >> 4) - (int32_t)t1_;
    int32_t v2 = (((d * d) >> 12) * t3_) >> 14;
    tFine = v1 + v2;
    return (tFine * 5 + 128) >> 8;
  }

  uint32_t pressure(int32_t adc, int32_t tFine) const {
    int64_t v1 = (int64_t)tFine - 128000;
    int64_t v2 = v1 * v1 * p_[4];                     // P6
    v2 += (v1 * p_[3]) << 17;                         // P5
    v2 += ((int64_t)p_[2]) << 35;                     // P4
    v1 = ((v1 * v1 * p_[1]) >> 8) + ((v1 * p_[0]) << 12);  // P3, P2
    v1 = ((((int64_t)1) << 47) + v1) * p1_ >> 33;
    if (v1 == 0) return 0;
    int64_t p = 1048576 - adc;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = ((int64_t)p_[7] * (p >> 13) * (p >> 13)) >> 25;  // P9
    v2 = ((int64_t)p_[6] * p) >> 19;                      // P8
    return (uint32_t)(((p + v1 + v2) >> 8) + ((int64_t)p_[5] << 4));  // P7
  }

  uint32_t humidity(int32_t adc, int32_t tFine) const {
    int32_t v = tFine - 76800;
    v = (((((adc << 14) - ((int32_t)h4_ << 20) - ((int32_t)h5_ * v)) + 16384) >> 15) *
         (((((((v * h6_) >> 10) * (((v * (int32_t)h3_) >> 11) + 32768)) >> 10) + 2097152) * h2_ + 8192) >> 14));
    v -= (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)h1_) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    return (uint32_t)(v >> 12);
  }

  uint16_t t1_ = 0, p1_ = 0;
  int16_t t2_ = 0, t3_ = 0, p_[8] = {}, h2_ = 0, h4_ = 0, h5_ = 0;
  uint8_t h1_ = 0, h3_ = 0;
  int8_t h6_ = 0;
  bool hum_ = true, told_ = false;
};
