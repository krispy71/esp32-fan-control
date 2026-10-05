#pragma once
#include "Arduino.h"
#include <functional>
class BLEUUID { public: explicit BLEUUID(const char*) {} };
class BLEAddress { public: std::string toString() const { return "01:23:45:67:89:AB"; } };
class BLEAdvertisedDevice {
public:
    BLEAddress getAddress() { return {}; }
    bool haveName() { return false; }
    std::string getName() { return {}; }
    bool haveServiceData() { return false; }
    std::string getServiceData() { return {}; }
    bool haveManufacturerData() { return false; }
    std::string getManufacturerData() { return {}; }
};
class BLEAdvertisedDeviceCallbacks { public: virtual ~BLEAdvertisedDeviceCallbacks() = default; virtual void onResult(BLEAdvertisedDevice) {} };
class BLEScanResults { public: int getCount() { return 0; } BLEAdvertisedDevice getDevice(int) { return {}; } };
class BLEScan {
public:
    void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks*) {}
    void setActiveScan(bool) {}
    void setInterval(int) {}
    void setWindow(int) {}
    BLEScanResults start(int, bool) { return {}; }
    void start(int, void*, bool) {}
};
class BLERemoteCharacteristic {
public:
    bool canRead() { return false; }
    bool canNotify() { return false; }
    std::string readValue() { return {}; }
    void registerForNotify(std::function<void(BLERemoteCharacteristic*, uint8_t*, size_t, bool)>) {}
};
class BLERemoteService { public: BLERemoteCharacteristic* getCharacteristic(BLEUUID) { return nullptr; } };
class BLEClient {
public:
    bool connect(BLEAdvertisedDevice*) { return false; }
    bool isConnected() { return false; }
    void disconnect() {}
    BLERemoteService* getService(BLEUUID) { return nullptr; }
};
class BLEDevice {
public:
    static void init(const char*) { Spy::event("bleBegin"); }
    static BLEScan* getScan() { static BLEScan scan; return &scan; }
    static BLEClient* createClient() { static BLEClient client; return &client; }
};
