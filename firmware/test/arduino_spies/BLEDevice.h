#pragma once
#include "Arduino.h"
#include <functional>
#include <set>
using esp_ble_addr_type_t=uint8_t;
class BLEUUID { public: explicit BLEUUID(const char*) {} };
class BLEAddress {
    std::string value_;
public:
    explicit BLEAddress(std::string value="01:23:45:67:89:AB") : value_(std::move(value)) {}
    std::string toString() const { return value_; }
};
class BLEAdvertisedDevice {
public:
    std::string mac{"01:23:45:67:89:AB"}, name, service_data, manufacturer_data;
    uint8_t address_type{0};
    BLEAddress getAddress() { return BLEAddress(mac); }
    uint8_t getAddressType() { return address_type; }
    bool haveName() { return !name.empty(); }
    std::string getName() { return name; }
    bool haveServiceData() { return !service_data.empty(); }
    std::string getServiceData() { return service_data; }
    bool haveManufacturerData() { return !manufacturer_data.empty(); }
    std::string getManufacturerData() { return manufacturer_data; }
};
class BLEAdvertisedDeviceCallbacks { public: virtual ~BLEAdvertisedDeviceCallbacks() = default; virtual void onResult(BLEAdvertisedDevice) {} };
class BLEScanResults { public: int getCount() { return 0; } BLEAdvertisedDevice getDevice(int) { return {}; } };
class BLEScan {
public:
    BLEAdvertisedDeviceCallbacks* callback{};
    bool duplicates=false, running=false;
    unsigned blocking_scans=0;
    std::map<std::string,BLEAdvertisedDevice> retained;
    void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks* value,bool want_duplicates=false) { callback=value; duplicates=want_duplicates; }
    void setActiveScan(bool) {}
    void setInterval(int) {}
    void setWindow(int) {}
    BLEScanResults start(int, bool) { ++blocking_scans; return {}; }
    void start(int, void*, bool) { retained.clear(); running=true; }
    void stop() { running=false; }
    // Arduino BLEScan.cpp calls the callback, then retains unseen addresses if
    // wantDuplicates is false. Filtering inside the callback does not alter this.
    void advertise(BLEAdvertisedDevice device) {
        if(!running || (!duplicates && retained.count(device.mac))) return;
        if(callback) callback->onResult(device);
        if(!duplicates) retained.emplace(device.mac,device);
    }
};
class BLERemoteCharacteristic {
public:
    bool canRead() { return true; }
    bool canNotify() { return false; }
    std::string readValue() { return std::string("\x38\x06",2); }
    void registerForNotify(std::function<void(BLERemoteCharacteristic*, uint8_t*, size_t, bool)>) {}
};
class BLERemoteService { public: BLERemoteCharacteristic* getCharacteristic(BLEUUID) { static BLERemoteCharacteristic characteristic; return &characteristic; } };
class BLEClient {
public:
    bool connected=false, allow_connection=false;
    std::string connected_mac;
    uint8_t connected_address_type=0;
    unsigned attempts=0;
    unsigned discoveries=0;
    bool services_known=false, cached_service_available=false;
    std::set<std::string> missing_service_macs;
    bool connect(BLEAddress address,esp_ble_addr_type_t address_type) {
        ++attempts; connected_mac=address.toString(); connected_address_type=address_type;
        connected=allow_connection; return connected;
    }
    bool isConnected() { return connected; }
    void disconnect() { connected=false; }
    void getServices() { ++discoveries; services_known=true; cached_service_available=connected && !missing_service_macs.count(connected_mac); }
    BLERemoteService* getService(BLEUUID) { if(!services_known) getServices(); static BLERemoteService service; return cached_service_available ? &service : nullptr; }
};
class BLEDevice {
public:
    static void init(const char*) { Spy::event("bleBegin"); }
    static BLEScan* getScan() { static BLEScan scan; return &scan; }
    static BLEClient* createClient() { static BLEClient client; return &client; }
};
