#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct WiFiSpy { int status() { return 0; } };
inline WiFiSpy WiFi;
using String = std::string;
