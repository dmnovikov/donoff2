// The settings of a device. Defaults are those of donoff-lib.
#pragma once
#include <stdint.h>

struct DSettings {
  char mqttServer[32] = "mqtt.lab240.ru";
  char mqttUser[16] = "";
  char mqttPass[32] = "";
  char mqttPort[6] = "1883";
  char devId[16] = "empty_dev";
  char email[50] = "";
  char devType[12] = "";                 // empty = what the board says

  int32_t aofs1 = 0, aofs2 = 0;          // seconds, 0 = off
  int32_t lschm1 = 0, lschm2 = 0;        // schedule of a relay: 0 none, 1..20 presets, 99 and 100 custom
  int32_t nosync1 = 0, nosync2 = 0, nosync3 = 0, nosync4 = 0;  // a schedule without time: 0 relay off, 1 relay on
  int32_t hotter = 0, cooler = 0;        // hotter: 1 day/night levels, 2 the level of every hour
  int32_t ctempl = 20, dtempl = 18, ntempl = 21;  // < 100 are degrees, otherwise 1/100 of a degree
  int32_t tdelta = 10;                   // < 50 are tenths, otherwise 1/100 of a degree
  int32_t onoffschm = 99;                // the scheme that says what is day and what is night
  int32_t tz = 3;
  int32_t autoreboot = 0;
  int32_t mbtcp = 0, mbport = 502, mbunit = 1;               // Modbus TCP, read only
  int32_t rtubaud = 9600, rtuparity = 0, rtustop = 1, rtuaddr = 1;  // Modbus RTU: only stored for now
  int32_t i2csda = -1, i2cscl = -1;    // the I2C bus, -1 = what the board says; used after a restart
  int32_t rturx = -1, rtutx = -1;      // the UART of RTU sensors (pins of the ESP), the same way
  int32_t clevel[4] = {-128, -128, -128, -128};  // what they mean is up to the board
  uint32_t cschm1 = 0b000000000011111111111100;  // bit (23 - h) is hour h
  uint32_t cschm2 = 0b111111111111100000000000;
  int32_t tmatrix[24] = {22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
                         22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 21};
};
