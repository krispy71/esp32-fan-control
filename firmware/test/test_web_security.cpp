#include "Adapters/Network/WebServerAdapter.hpp"
#include "Services/SmokerControlService.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
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
    float pit_f{201.25f}, meat_f{143.5f};
    Domain::SensorFault pit_fault{Domain::SensorFault::Ok}, meat_fault{Domain::SensorFault::Ok};
    Domain::TemperatureReading readTemperature(Domain::SensorRole role) override {
        const bool pit = role == Domain::SensorRole::Pit;
        return Domain::TemperatureReading::fromFahrenheit(pit ? pit_f : meat_f, role, 1000,
                                                         pit ? pit_fault : meat_fault);
    }
};
struct Damper : Services::Ports::IDamperActuatorPort {
    Domain::DamperCalibration calibration{};
    float position{0};
    void configure(const Domain::DamperCalibration& c) override { calibration=c; }
    void setPosition(float value) override { position=value; }
};
struct Blower : Services::Ports::IBlowerActuatorPort {
    float speed{0};
    void setSpeed(float value) override { speed=value; }
};
struct Storage : Services::Ports::IConfigStoragePort {
    bool fail{false};
    Domain::SmokerConfig saved{};
    unsigned saves{0};
    bool loadConfig(Domain::SmokerConfig&) override { return false; }
    bool saveConfig(const Domain::SmokerConfig& config) override {
        if (fail) return false;
        saved=config; ++saves; return true;
    }
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
    assert(route.handler(&req)==ESP_FAIL); // IDF must close, never drain an unread body.
    assert(req.response_headers["Connection"]=="close");
    return req;
}
struct ResponseJson {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> value;
    explicit ResponseJson(const httpd_req_t& response)
        : value(cJSON_Parse(response.response.c_str()), cJSON_Delete) { assert(cJSON_IsObject(value.get())); }
    const cJSON* field(const char* name) const {
        const auto* item=cJSON_GetObjectItemCaseSensitive(value.get(), name); assert(item); return item;
    }
    double number(const char* name) const { const auto* item=field(name); assert(cJSON_IsNumber(item)); return item->valuedouble; }
    bool boolean(const char* name) const { const auto* item=field(name); assert(cJSON_IsBool(item)); return cJSON_IsTrue(item); }
    std::string string(const char* name) const { const auto* item=field(name); assert(cJSON_IsString(item)); return item->valuestring; }
};
std::string queueCommand(const char* path, const std::string& fields, uint32_t version) {
    const auto response=request(path, HTTP_POST, "{\"config_version\":"+std::to_string(version)+","+fields+"}");
    assert(response.status=="202 Accepted");
    const ResponseJson queued(response);
    assert(queued.string("status")=="queued");
    const auto result_path="/api/command?id="+std::to_string(static_cast<unsigned>(queued.number("request_id")));
    const auto pending=request(result_path.c_str());
    assert(pending.status=="202 Accepted" && ResponseJson(pending).string("status")=="queued");
    return result_path;
}
void assertApplied(const std::string& path, uint32_t version) {
    const auto response=request(path.c_str());
    assert(response.status=="200 OK");
    const ResponseJson result(response);
    assert(result.string("status")=="applied");
    assert(result.string("persistence")=="saved");
    assert(result.number("config_version")==version);
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
    assert(web.begin()); assert(httpd_settings.server_port==443 && httpd_settings.max_open_sockets==2);
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
    storage.fail=false;

    // Serialize changing sensor values from owner-published snapshots, not defaults.
    for (unsigned i=0; i<2; ++i) {
        sensor.pit_f=201.25f+6.25f*i;
        sensor.meat_f=143.5f+8.25f*i;
        service.executeCycle(4000+1000*i);
        const auto response=request("/api/telemetry");
        assert(response.status=="200 OK");
        const ResponseJson telemetry(response);
        assert(std::abs(telemetry.number("pit_temp_f")-sensor.pit_f)<0.001);
        assert(std::abs(telemetry.number("meat_temp_f")-sensor.meat_f)<0.001);
        assert(telemetry.number("timestamp_ms")==4000+1000*i);
        assert(telemetry.boolean("is_pit_valid") && telemetry.boolean("is_meat_valid"));
        assert(telemetry.string("status")=="REGULATING");
    }

    uint32_t now=6000;
    for (float target : {175.0f, 275.5f}) {
        const auto before=service.config();
        const auto saves=storage.saves;
        const auto version=channel.state.config_version;
        const auto result_path=queueCommand("/api/setpoint", "\"setpoint\":"+std::to_string(target), version);
        assert(service.setpoint()==before.setpoint_f && storage.saves==saves);
        assert(ResponseJson(request("/api/config")).number("setpoint_f")==before.setpoint_f);
        service.executeCycle(now); now+=1000;
        assertApplied(result_path, version+1);
        assert(service.setpoint()==target && storage.saved.setpoint_f==target && storage.saves==saves+1);
        assert(ResponseJson(request("/api/config")).number("setpoint_f")==target);
        const ResponseJson telemetry(request("/api/telemetry"));
        assert(telemetry.number("setpoint_f")==target);
        assert(telemetry.number("damper_position_pct")==damper.position);
        assert(telemetry.number("blower_speed_pct")==blower.speed);
        if (target < sensor.pit_f) assert(damper.position==0 && blower.speed==0);
        else assert(damper.position>0 && blower.speed>0);
    }
    const auto version=channel.state.config_version;
    const auto saves=storage.saves;
    for (const char* action : {"pause", "resume"}) {
        const bool pause=std::strcmp(action, "pause")==0;
        const auto result_path=queueCommand("/api/lid-pause", std::string("\"action\":\"")+action+"\"", version);
        assert(service.isLidOpen()!=pause); // Dispatch only queues work for the control owner.
        assert((damper.position==0 && blower.speed==0)!=pause);
        service.executeCycle(now); now+=1000;
        assertApplied(result_path, version);
        const ResponseJson telemetry(request("/api/telemetry"));
        assert(service.isLidOpen()==pause && telemetry.boolean("lid_open")==pause);
        assert(telemetry.string("status")== (pause ? "LID_OPEN" : "REGULATING"));
        assert(telemetry.number("damper_position_pct")==damper.position);
        assert(telemetry.number("blower_speed_pct")==blower.speed);
        if (pause) assert(damper.position==0 && blower.speed==0 && telemetry.number("demand_pct")==0);
        else assert(damper.position>0 && blower.speed>0 && telemetry.number("demand_pct")>0);
        assert(service.setpoint()==275.5f && storage.saves==saves);
    }

    // Fault flags and invalid values must be transported with the fail-safe state.
    sensor.pit_fault=Domain::SensorFault::Disconnected;
    sensor.meat_fault=Domain::SensorFault::ShortToGnd;
    service.executeCycle(now); now+=1000;
    const ResponseJson fault(request("/api/telemetry"));
    assert(!fault.boolean("is_pit_valid") && !fault.boolean("is_meat_valid"));
    assert(fault.number("pit_temp_f")==0 && fault.number("meat_temp_f")==0);
    assert(fault.string("status")=="FAULT: SENSOR_INVALID");
    assert(fault.number("damper_position_pct")==0 && fault.number("blower_speed_pct")==0);
    assert(damper.position==0 && blower.speed==0);
    sensor.pit_fault=Domain::SensorFault::Ok;
    sensor.meat_fault=Domain::SensorFault::Ok;
    sensor.pit_f=211.75f; sensor.meat_f=156.25f;
    service.executeCycle(now);
    const ResponseJson recovered(request("/api/telemetry"));
    assert(recovered.boolean("is_pit_valid") && recovered.boolean("is_meat_valid"));
    assert(std::abs(recovered.number("pit_temp_f")-211.75)<0.001);
    assert(std::abs(recovered.number("meat_temp_f")-156.25)<0.001);
    assert(recovered.string("status")=="REGULATING");
    std::cout << "Production HTTPS handler security, calibration, setpoint, pause/resume and telemetry checks passed\n";
}
