#include "Adapters/Network/WebServerAdapter.hpp"
#include "Services/SmokerControlService.hpp"
#include <cassert>
#include <deque>
#include <iostream>
using namespace SmokerController;
struct Channel : Services::Ports::IControlChannel {
    std::deque<Domain::ControlCommand> commands;
    Domain::ControlState state{};
    bool submit(const Domain::ControlCommand& c) override { if(commands.size()==8) return false; commands.push_back(c); return true; }
    bool receive(Domain::ControlCommand& c) override { if(commands.empty()) return false; c=commands.front(); commands.pop_front(); return true; }
    void publish(const Domain::ControlState& s) override { state=s; }
    bool snapshot(Domain::ControlState& s) const override { s=state; return true; }
};
struct Sensor : Services::Ports::ITemperatureSensorPort {
    Domain::TemperatureReading readTemperature(Domain::SensorRole role) override { return Domain::TemperatureReading::fromFahrenheit(225,role,1000); }
};
struct Damper : Services::Ports::IDamperActuatorPort {
    Domain::DamperCalibration calibration{};
    void configure(const Domain::DamperCalibration& c) override { calibration=c; }
    void setPosition(float) override {}
};
struct Blower : Services::Ports::IBlowerActuatorPort { void setSpeed(float) override {} };
struct Storage : Services::Ports::IConfigStoragePort {
    bool fail{false};
    bool loadConfig(Domain::SmokerConfig&) override { return false; }
    bool saveConfig(const Domain::SmokerConfig&) override { return !fail; }
};
std::string password="test-material-only-no-real-secret";
std::string auth() {
    const std::string plaintext="admin:"+password;
    unsigned char encoded[256]{}; size_t size=0;
    assert(mbedtls_base64_encode(encoded,sizeof(encoded),&size,reinterpret_cast<const unsigned char*>(plaintext.data()),plaintext.size())==0);
    return "Basic "+std::string(reinterpret_cast<char*>(encoded),size);
}
httpd_req_t request(const char* path,int method=HTTP_GET,std::string body="",bool authenticate=true,std::string origin="",std::string host="192.168.4.1") {
    auto route=routes[method==HTTP_POST?1:0];
    httpd_req_t req{path,method,route.user_ctx,body.size(),body,{}, {}, "", "200 OK",0};
    req.headers["Host"]=host;
    if(authenticate) req.headers["Authorization"]=auth();
    if(!origin.empty()) req.headers["Origin"]=origin;
    req.headers["Content-Type"]="application/json";
    assert(route.handler(&req)==ESP_OK);
    return req;
}
int main() {
    Channel channel; Sensor sensor; Damper damper; Blower blower; Storage storage;
    Services::SmokerControlService service(sensor,damper,blower,nullptr,225,Domain::ActuatorCoordinator{}, Domain::PIDConfig{}, &storage,&channel);
    service.initialize(); service.executeCycle(1000);
    Adapters::Network::WebServerAdapter web(channel);
    assert(!web.begin()); assert(!WiFi.started); // No provisioned credentials means no listener/AP.
    unsigned char digest[32]{}; char hash[65]{};
    mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(password.data()),password.size(),digest,0);
    for(size_t i=0;i<32;++i) std::snprintf(hash+2*i,3,"%02x",digest[i]);
    LittleFS.files["/device-access.json"]=std::string("{\"ap_ssid\":\"device-test\",\"ap_password\":\"device-specific-test-wifi\",\"username\":\"admin\",\"password_sha256\":\"")+hash+"\",\"hosts\":[\"192.168.4.1\"]}";
    LittleFS.files["/device-cert.pem"]="test certificate supplied to TLS boundary";
    LittleFS.files["/device-key.pem"]="test key supplied to TLS boundary";
    LittleFS.files["/index.html"]="dashboard";
    assert(web.begin()); assert(tls_settings.port_secure==443 && tls_settings.httpd.max_open_sockets==2);
    for(const auto* path:{"/","/api/config","/api/telemetry","/device-key.pem"}) assert(request(path,HTTP_GET,"",false).status=="401 Unauthorized");
    assert(request("/api/setpoint",HTTP_POST,"{\"setpoint\":450,\"config_version\":0}",false).status=="401 Unauthorized");
    assert(request("/api/config",HTTP_GET,"",true,"https://attacker.example").status=="403 Forbidden");
    assert(request("/api/config",HTTP_GET,"",true,"","attacker.example").status=="403 Forbidden");
    assert(request("/device-key.pem").status=="404 Not Found");
    assert(request("/device-access.json").status=="404 Not Found");
    auto old_password=password; password="wrong-password";
    assert(request("/api/config").status=="401 Unauthorized"); password=old_password;
    for(const auto* body:{"{\"config_version\":0,\"servo_min_pulse_us\":2400,\"servo_max_pulse_us\":1000}",
        "{\"config_version\":0,\"servo_min_pulse_us\":1000.5}","{\"config_version\":0,\"servo_inverted\":\"false\"}",
        "{\"config_version\":0,\"unknown\":5}","{\"config_version\":0,\"pid_kp\":1,\"pid_kp\":2}",
        "{\"config_version\":0,\"pid_kp\":NaN}","{\"config_version\":0,\"pid_kp\":{\"x\":[1]}}"}) {
        assert(request("/api/config",HTTP_POST,body).status=="400 Bad Request"); assert(channel.commands.empty());
    }
    assert(request("/api/config",HTTP_POST,std::string(2049,' ')).status=="413 Payload Too Large");
    auto accepted=request("/api/config",HTTP_POST,"{\"config_version\":0,\"servo_min_pulse_us\":800,\"servo_max_pulse_us\":2200,\"servo_inverted\":true,\"meater_cloud_token\":\"test-placeholder\"}");
    assert(accepted.status=="202 Accepted");
    assert(accepted.response.find("test-placeholder")==std::string::npos);
    assert(service.config().servo_min_pulse_us==1000); // No service mutation from the network task.
    assert(request("/api/command?id=1").status=="202 Accepted");
    service.executeCycle(2000);
    assert(damper.calibration.min_pulse_us==800 && damper.calibration.inverted);
    auto result=request("/api/command?id=1");
    assert(result.response.find("\"status\":\"applied\"")!=std::string::npos && result.response.find("\"persistence\":\"saved\"")!=std::string::npos);
    auto config=request("/api/config");
    assert(config.response.find("test-placeholder")==std::string::npos);
    assert(config.response.find("\"meater_cloud_token\":")==std::string::npos);
    assert(config.response.find("\"meater_cloud_token_configured\":true")!=std::string::npos);
    assert(request("/api/config",HTTP_POST,"{\"config_version\":0,\"pid_kp\":5}").status=="409 Conflict");
    storage.fail=true;
    assert(request("/api/config",HTTP_POST,"{\"config_version\":1,\"pid_kp\":5}").status=="202 Accepted");
    service.executeCycle(3000);
    assert(request("/api/command?id=2").response.find("\"persistence\":\"failed\"")!=std::string::npos);
    std::cout << "Production HTTPS handler security/calibration checks passed\n";
}
