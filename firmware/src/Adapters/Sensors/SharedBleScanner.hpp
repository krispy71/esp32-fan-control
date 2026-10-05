#pragma once
#ifdef ARDUINO
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

namespace SmokerController::Adapters::Sensors {
// One owner for the SDK's singleton scanner. It forwards observations without
// collecting devices, and coordinates pauses around a direct GATT connection.
class SharedBleScanner
#ifdef ARDUINO
    : public BLEAdvertisedDeviceCallbacks
#endif
{
public:
#ifdef ARDUINO
    void begin(BLEAdvertisedDeviceCallbacks& passive, BLEAdvertisedDeviceCallbacks& discovery) {
        if (scan_) return;
        passive_ = &passive;
        discovery_ = &discovery;
        BLEDevice::init("SmokerController");
        scan_ = BLEDevice::getScan();
        // false retains all distinct addresses, even when callbacks reject them.
        scan_->setAdvertisedDeviceCallbacks(this, true);
        scan_->setActiveScan(false);
        scan_->setInterval(100);
        scan_->setWindow(99);
        resume();
    }
    void pause() { if (scan_) scan_->stop(); }
    void resume() { if (scan_) scan_->start(0, nullptr, false); }
    void onResult(BLEAdvertisedDevice device) override {
        passive_->onResult(device);
        discovery_->onResult(device);
    }
private:
    BLEScan* scan_{nullptr};
    BLEAdvertisedDeviceCallbacks* passive_{nullptr};
    BLEAdvertisedDeviceCallbacks* discovery_{nullptr};
#endif
};
}
