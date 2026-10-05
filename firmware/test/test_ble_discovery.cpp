#include <cassert>
#include <cstdio>
#include <iostream>
#include "../src/Adapters/Sensors/BLEProbeAdapter.hpp"
#include "../src/Adapters/Sensors/MeaterBleClientAdapter.hpp"
using namespace SmokerController;

int main() {
    Adapters::Sensors::BLEProbeAdapter passive;
    Adapters::Sensors::SharedBleScanner scanner;
    Adapters::Sensors::MeaterBleClientAdapter direct{30000, &scanner};
    scanner.begin(passive, direct);
    auto* scan=BLEDevice::getScan();
    auto* client=BLEDevice::createClient();
    assert(scan->duplicates && scan->running);
    direct.setEnabled(true);
    // Exercise the installed library's post-callback retention contract. Neither
    // non-probe advertisers nor matching candidates may accumulate in its map.
    for(unsigned n=0;n<300;++n) {
        BLEAdvertisedDevice device;
        char mac[18]; std::snprintf(mac,sizeof(mac),"01:23:45:67:%02X:%02X",n/256,n%256);
        device.mac=mac;
        scan->advertise(device);
    }
    Spy::now=16000;
    direct.update(Spy::now);
    assert(client->attempts==0 && scan->retained.empty());

    BLEAdvertisedDevice meater;
    meater.mac="AA:BB:CC:DD:EE:FF"; meater.name="MEATER test"; meater.address_type=1;
    meater.service_data=std::string("\x38\x06",2);
    scan->advertise(meater);
    client->allow_connection=true;
    direct.update(Spy::now);
    assert(client->attempts==1 && client->connected_mac==meater.mac && client->connected_address_type==1);
    assert(scan->blocking_scans==0 && scan->retained.empty() && scan->running && scan->duplicates);
    assert(direct.readTemperature(Domain::SensorRole::Food1).celsius==100);

    // The passive callback remains installed and receives subsequent packets
    // from the same address after a direct discovery/connect attempt.
    meater.service_data=std::string("\x38\x07",2);
    scan->advertise(meater);
    assert(passive.lastReading().internal_temp_c==18.48f);
    assert(scan->retained.empty());

    direct.setTargetMac("11:22:33:44:55:66");
    direct.update(Spy::now); // Disconnect the previous configured identity.
    Spy::now=32000;
    scan->advertise(meater);
    direct.update(Spy::now);
    assert(client->attempts==1);
    BLEAdvertisedDevice selected;
    selected.mac="11:22:33:44:55:66"; selected.address_type=0; // A MAC match need not advertise a name.
    scan->advertise(selected);
    direct.update(Spy::now);
    assert(client->attempts==2 && client->connected_mac==selected.mac && client->connected_address_type==0);

    direct.setEnabled(false);
    direct.update(Spy::now);
    Spy::now=48000;
    scan->advertise(selected);
    direct.setEnabled(true);
    direct.update(Spy::now);
    assert(client->attempts==2); // No candidate collected while disabled.
    scan->advertise(selected);
    Spy::now=64001;
    direct.update(Spy::now);
    assert(client->attempts==2); // Old candidates cannot reconnect a vanished probe.
    scan->advertise(selected);
    direct.update(Spy::now);
    assert(client->attempts==3 && scan->retained.empty());

    // SDK discovery is cached across connect/disconnect: an incorrect configured
    // device must not poison later discovery after the operator corrects its MAC.
    const char* wrong_mac="77:88:99:AA:BB:CC";
    client->missing_service_macs.insert(wrong_mac);
    direct.setTargetMac(wrong_mac);
    Spy::now=80000; direct.update(Spy::now);
    selected.mac=wrong_mac; scan->advertise(selected); direct.update(Spy::now);
    assert(client->attempts==4 && !direct.readTemperature(Domain::SensorRole::Food1).isValid());
    direct.setTargetMac(meater.mac.c_str());
    Spy::now=96000; direct.update(Spy::now);
    scan->advertise(meater); direct.update(Spy::now);
    assert(client->attempts==5 && direct.readTemperature(Domain::SensorRole::Food1).isValid());
    assert(client->discoveries==client->attempts);
    std::cout << "Bounded BLE scan retention and direct candidate selection checks passed\n";
}
