#pragma once
#define WIFI_AP 1
struct WiFiSpy {
    bool started{false};
    void mode(int) {}
    bool softAP(const char*, const char*) { started = true; return true; }
    void softAPdisconnect(bool) { started = false; }
};
inline WiFiSpy WiFi;
