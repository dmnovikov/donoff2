// The value the thermostat follows: any value in C, chosen by its name (setting tsensor).
#pragma once
#include <string.h>
#include <string>
#include <vector>
#include "dsensor.h"

namespace dthermo {
inline bool isTemp(const DValue& v) { return v.unit && strcmp(v.unit, "C") == 0; }

inline DValue* find(std::vector<DSensor*>& sensors, const char* name) {
  for (auto* s : sensors)
    for (size_t i = 0; i < s->count(); i++)
      if (isTemp(s->value(i)) && strcmp(s->value(i).name, name) == 0) return &s->value(i);
  return nullptr;
}

// the name in use: the setting, or the default of the board
inline const char* wanted(const DContext& ctx) { return ctx.s.tsensor[0] ? ctx.s.tsensor : ctx.tsensorDefault; }

inline std::string list(std::vector<DSensor*>& sensors) {
  std::string t;
  for (auto* s : sensors)
    for (size_t i = 0; i < s->count(); i++)
      if (isTemp(s->value(i))) t += (t.empty() ? "" : ",") + std::string(s->value(i).name);
  return t;
}
}  // namespace dthermo
