#pragma once
#include "Arduino.h"
#include <cstring>
namespace Spy {
inline std::map<std::string, std::vector<uint8_t>> nvs;
inline bool nvsOpenFailure = false;
inline bool nvsWriteFailure = false;
inline bool nvsReadFailure = false;
}
class Preferences {
public:
    bool begin(const char* name, bool) { Spy::event("nvsBegin"); ns_ = name; return !Spy::nvsOpenFailure; }
    void end() { Spy::event("nvsEnd"); }
    bool isKey(const char* key) { return Spy::nvs.count(ns_ + key) != 0; }
    size_t getBytesLength(const char* key) { return Spy::nvs[ns_ + key].size(); }
    size_t getBytes(const char* key, void* out, size_t size) { if (Spy::nvsReadFailure) return 0; const auto& bytes = Spy::nvs[ns_ + key]; size = std::min(size, bytes.size()); std::memcpy(out, bytes.data(), size); return size; }
    size_t putBytes(const char* key, const void* value, size_t size) { if (Spy::nvsWriteFailure) return 0; const auto* bytes = static_cast<const uint8_t*>(value); Spy::nvs[ns_ + key] = std::vector<uint8_t>(bytes, bytes + size); return size; }
    float getFloat(const char*, float value) { return value; }
    uint32_t getUInt(const char*, uint32_t value) { return value; }
private: std::string ns_;
};
