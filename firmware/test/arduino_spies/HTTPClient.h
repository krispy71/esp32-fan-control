#pragma once
#include "WiFi.h"
#include "WiFiClientSecure.h"
#define HTTP_CODE_OK 200
class HTTPClient {
public:
    bool begin(WiFiClientSecure&, const char*) { return false; }
    void addHeader(const char*, const char*) {}
    void setTimeout(int) {}
    int GET() { return 0; }
    String getString() { return {}; }
    void end() {}
};
