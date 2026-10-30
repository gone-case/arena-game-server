#pragma once
#include <cstdlib>
#include <string>

inline std::string env_or(const char* key, const std::string& def) {
  const char* v = std::getenv(key);
  return v && *v ? v : def;
}
inline int env_int(const char* key, int def) {
  const char* v = std::getenv(key);
  return v && *v ? std::atoi(v) : def;
}
