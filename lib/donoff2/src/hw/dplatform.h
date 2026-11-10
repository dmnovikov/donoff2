// What differs between ESP8266 and ESP32.
#pragma once
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#define DONOFF_ADC_MAX 1024
#else
#include <WiFi.h>
#define DONOFF_ADC_MAX 4096
#endif
