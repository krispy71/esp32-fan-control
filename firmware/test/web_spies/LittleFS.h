#pragma once
#include "Arduino.h"
#include <map>
#include <cstring>
struct File {
    std::string body;
    size_t position{0};
    bool exists{false};
    explicit operator bool() const { return exists; }
    size_t size() const { return body.size(); }
    String readString() { return body; }
    bool available() const { return position < body.size(); }
    size_t readBytes(char* target, size_t maximum) {
        size_t count = std::min(maximum, body.size() - position);
        std::memcpy(target, body.data() + position, count); position += count; return count;
    }
};
struct FilesystemSpy {
    std::map<std::string, std::string> files;
    bool begin(bool format) { return !format; }
    File open(const char* path, const char*) {
        auto found = files.find(path);
        return found == files.end() ? File{} : File{found->second, 0, true};
    }
};
inline FilesystemSpy LittleFS;
