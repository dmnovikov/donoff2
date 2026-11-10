// Where the settings live. The text comes from DCommands.
#pragma once
#include <string>

class DStorage {
 public:
  virtual ~DStorage() = default;
  virtual bool load(std::string& text) = 0;   // false: nothing stored
  virtual bool save(const std::string& text) = 0;
};
