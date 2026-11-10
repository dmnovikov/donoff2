// A power cut during a write leaves either the old file or the new one: tmp + rename on LittleFS.
#pragma once
#include <LittleFS.h>
#include "../dstorage.h"

class DStorageLittleFS : public DStorage {
 public:
#if defined(ESP8266)
  bool begin() { return LittleFS.begin(); }
#else
  bool begin() { return LittleFS.begin(true); }  // formats an empty partition
#endif

  bool load(std::string& text) override {
    File f = LittleFS.open(kFile, "r");
    if (!f) return false;
    text.clear();
    text.reserve(f.size());
    while (f.available()) text += (char)f.read();
    f.close();
    return !text.empty();
  }

  bool save(const std::string& text) override {
    File f = LittleFS.open(kTmp, "w");
    if (!f) return false;
    size_t n = f.write((const uint8_t*)text.data(), text.size());
    f.close();
    if (n != text.size()) return false;
    return LittleFS.rename(kTmp, kFile);
  }

 private:
  static constexpr const char* kFile = "/settings.txt";
  static constexpr const char* kTmp = "/settings.tmp";
};
