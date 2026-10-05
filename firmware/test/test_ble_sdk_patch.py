"""Execute the patched vendor discovery/cleanup bodies with allocation tracking."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ble_patch", root / "tools/patch_ble_client.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
framework = Path(os.environ.get("ARDUINO_FRAMEWORK_DIR", str(Path.home() / ".platformio/packages/framework-arduinoespressif32")))
original = (framework / "libraries/BLE/src/BLEClient.cpp").read_bytes()
source = patch.patch_source(original)
try:
    patch.patch_source(original + b"\n// different SDK")
except RuntimeError:
    pass
else:
    raise AssertionError("Unreviewed SDK source must fail the build")


def body(start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


# These are the actual vendor method/event bodies emitted by the build patch,
# not a second implementation of their ownership logic.
destructor = body("BLEClient::~BLEClient() {", "} // ~BLEClient")
cleanup = body("void BLEClient::clearServices() {", "} // clearServices")
discovery = body("case ESP_GATTC_SEARCH_RES_EVT: {", "} // ESP_GATTC_SEARCH_RES_EVT")
harness = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#define log_v(...) ((void)0)
struct ServiceId { std::string uuid; uint16_t inst_id; };
struct Event { struct { int conn_id; ServiceId srvc_id; int start_handle, end_handle; } search_res; };
struct BLEUUID { ServiceId id; explicit BLEUUID(ServiceId value):id(value){} std::string toString() const {return id.uuid;} };
struct BLERemoteService {
    static inline int live=0;
    BLERemoteService(ServiceId, void*, int, int) { ++live; }
    ~BLERemoteService() { --live; }
};
class BLEClient {
public:
    std::map<std::string, BLERemoteService*> m_servicesMap;
    std::map<BLERemoteService*, uint16_t> m_servicesMapByInstID;
    bool m_haveServices=true;
    int getConnId() { return 0; }
    ~BLEClient() { DESTRUCTOR }
    void clearServices() { CLEANUP }
    void discover(Event* evtParam) { switch(0) { case 0: { DISCOVERY } } }
};
int main() {
    {
        BLEClient client;
        for(int round=0; round<200; ++round) {
            client.clearServices();
            assert(BLERemoteService::live==0 && client.m_servicesMapByInstID.empty());
            assert(!client.m_haveServices);
            for(int instance=0; instance<64; ++instance) {
                for(const char* uuid : {"meater", "battery", "device-info"}) {
                    Event event{{0,{uuid,static_cast<uint16_t>(instance)},1,2}};
                    client.discover(&event);
                }
                assert(BLERemoteService::live==3);
                assert(client.m_servicesMap.size()==3 && client.m_servicesMapByInstID.size()==3);
            }
        }
    }
    assert(BLERemoteService::live==0);
    std::cout << "Patched Arduino BLE discovery: repeated duplicate instances retain only owned services\n";
}
'''.replace("DESTRUCTOR", destructor).replace("CLEANUP", cleanup).replace("DISCOVERY", discovery)

with tempfile.TemporaryDirectory(prefix="smoker-ble-sdk-") as scratch:
    cpp = Path(scratch) / "discovery.cpp"
    binary = Path(scratch) / "discovery"
    cpp.write_text(harness)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
