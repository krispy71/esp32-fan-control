#pragma once
#include <string>
#include <cstdint>
using String = std::string;
struct SerialSpy { void println(const char*) {} };
inline SerialSpy Serial;
