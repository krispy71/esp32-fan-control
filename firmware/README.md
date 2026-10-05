# ESP32 Smoker Controller Firmware & Web Interface

Embedded C++17 firmware and offline-first web dashboard for the ESP32 Smoker Fan & Servo Damper Controller, structured using the PocketSWE Onion Architecture.

---

## 1. Directory Structure

```text
firmware/
├── platformio.ini              # PlatformIO build configuration (esp32dev / esp32dev-max31856 / native)
├── partitions.csv              # Custom 4MB partition table (2.5MB App + 1.44MB LittleFS)
├── include/                    # Common project headers
├── data/                       # LittleFS web dashboard static assets
│   ├── index.html              # Responsive pitmaster control dashboard
│   ├── style.css               # Dark theme responsive mobile-first stylesheet
│   └── app.js                  # Authenticated polling telemetry client & HTML5 canvas trend graph
├── src/
│   ├── Domain/                 # Pure domain business logic (zero Arduino/ESP32 dependencies)
│   │   ├── Temperature.hpp     # TemperatureReading, SensorRole, SensorFault
│   │   ├── Airflow.hpp         # AirflowDemand, ActuatorTargets, ActuatorCoordinator
│   │   ├── Configuration.hpp   # SmokerConfig domain model and validation invariants
│   │   ├── PID.hpp             # Discrete PID regulator with anti-windup clamping
│   │   ├── LidDetector.hpp     # Lid-open suppression state machine
│   │   └── BLEDecoder.hpp      # Passive advertisement packet decoders (MEATER, Inkbird)
│   ├── Services/               # Core application orchestration
│   │   ├── Ports/              # Abstract hardware contracts
│   │   │   ├── TemperatureSensorPort.hpp
│   │   │   ├── DamperActuatorPort.hpp
│   │   │   ├── BlowerActuatorPort.hpp
│   │   │   ├── ConfigStoragePort.hpp
│   │   │   └── TelemetryPort.hpp
│   │   └── SmokerControlService.hpp
│   ├── Adapters/               # Concrete hardware, network, and storage drivers
│   │   ├── Actuators/
│   │   │   ├── ESP32ServoDamperAdapter.hpp # 50Hz LEDC PWM with idle-detach
│   │   │   └── ESP32PWMBlowerAdapter.hpp   # 25kHz ultrasonic MOSFET PWM
│   │   ├── Sensors/
│   │   │   ├── MAX31855SensorAdapter.hpp   # SPI thermocouple acquisition & fault decoding
│   │   │   ├── BLEProbeAdapter.hpp         # BLE passive scanner for meat probes
│   │   │   └── CompositeSensorAdapter.hpp  # Wireless primary with wired thermocouple fallback
│   │   ├── Telemetry/
│   │   │   └── SerialTelemetryAdapter.hpp  # Console/serial output formatter
│   │   ├── Storage/
│   │   │   └── ESP32NVSConfigAdapter.hpp   # Non-Volatile Storage (NVS) Preferences adapter
│   │   └── Network/
│   │       └── WebServerAdapter.hpp        # HTTPS REST endpoints & LittleFS allowlist
│   └── Controller/
│       └── main.cpp            # FreeRTOS task setup & pin bootstrap
└── test/
    └── test_domain.cpp         # Host-executable C++17 unit tests
```

---

## 2. Hardware Pinout Reference

> For complete point-to-point breadboard wiring and schematics, refer to [docs/wiring-diagram.md](../docs/wiring-diagram.md).

| Function | ESP32 GPIO | Peripheral / Mode | Notes |
| :--- | :---: | :--- | :--- |
| **Blower Fan Gate** | `GPIO 25` | LEDC Ch 0, 25 kHz PWM | Drives gate of N-MOSFET (AO3400A / IRLZ44N) |
| **Servo Damper Signal** | `GPIO 26` | LEDC Ch 2, 50 Hz PWM | Connects to RJ45 Pin 6 (MG90S micro-servo) |
| **SPI SCK** | `GPIO 18` | Hardware SPI Clock | Shared clock line for MAX31855 amplifiers |
| **SPI MISO** | `GPIO 19` | Hardware SPI Data | Shared serial data in from MAX31855 |
| **CS Pit Thermocouple** | `GPIO 5` | Active-low Chip Select | Dedicated CS for pit/chamber probe |
| **SPI MOSI** | `GPIO 23` | Hardware SPI Data | Required for display and MAX31856 |
| **CS Food Thermocouple** | `GPIO 21` | Active-low Chip Select | Optional; disabled unless `SMOKER_FOOD_CS=21` |

---

## 3. Web Dashboard & REST API Endpoints

The web dashboard is served over HTTPS from ESP32 LittleFS using a per-device SoftAP and administrator credential. Provision the private access files using [docs/device-access.md](../docs/device-access.md). Unprovisioned firmware keeps web access disabled while wired control remains autonomous.

### HTTP Endpoints

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/` | Serves dashboard HTML (`index.html`) |
| `GET` | `/style.css` | Serves dark theme CSS |
| `GET` | `/app.js` | Serves real-time JavaScript frontend |
| `GET` | `/api/telemetry` | Returns instantaneous JSON state snapshot |
| `GET` | `/api/command?id=...` | Poll queued/applied/rejected command and persistence result |
| `GET` | `/api/config` | Read active tuning, calibration, and configuration version; cloud token redacted |
| `POST` | `/api/setpoint` | Update pit setpoint: `{"setpoint": 225.0, "config_version": 0}` |
| `POST` | `/api/config` | Update tuning parameters and persist to NVS flash |
| `POST` | `/api/lid-pause` | `{"action":"pause","config_version":0}` or `"resume"` |

All routes require administrator authentication over TLS. The dashboard polls telemetry
at 1 Hz. Each POST requires the current `config_version` returned by `/api/config`;
stale versions return 409. A 202 response contains a `request_id`, indicating queued
acceptance. Poll `/api/command?id=...` until applied/rejected and check `persistence`:
`saved`, `failed`, `unchanged`, or `not_configured`. An applied setting can still fail to
persist; the dashboard reports this separately. A pending command prevents a second
submission until its result is collected. The last eight results are retained in RAM.

`POST /api/config` accepts tuning and calibration fields, including
`servo_min_pulse_us`, `servo_max_pulse_us`, and `servo_inverted`. Omit
`meater_cloud_token` to preserve it; send an empty string to remove it. API responses
expose only whether it is configured.

### Example Telemetry JSON Payload

```json
{
  "timestamp_ms": 142300,
  "pit_temp_f": 224.8,
  "meat_temp_f": 165.2,
  "setpoint_f": 225.0,
  "damper_position_pct": 42.0,
  "blower_speed_pct": 0.0,
  "demand_pct": 17.0,
  "is_pit_valid": true,
  "is_meat_valid": true,
  "lid_open": false,
  "status": "REGULATING",
  "is_meat_wireless": true,
  "meat_battery_pct": 92,
  "meat_probe_name": "MEATER+"
}
```

---

## 4. FreeRTOS Task Architecture

* **Core 1 — Control task (`SmokerControl`)**, priority 2: owns the control service,
  actuator state, and wired sensor sampling. It runs PID and normal telemetry at a nominal 1 Hz, checks pit safety between
  those cycles, and services converter/servo upkeep every 20 ms. Hardware transactions and task scheduling add
  latency; physical timing must be measured on the bench.
* **Core 0 — Network task (`SmokerNetwork`) and HTTPS server**: updates BLE/cloud
  caches, serves the dashboard, and renders the display. Web requests submit typed
  commands through a bounded queue and read copied state; they never mutate the
  control service directly. Wireless cache exchanges use short locks.
* **Shared SPI**: an explicit bus owner serializes complete display/converter frames,
  including transaction settings and chip-select lifetime. The display uses MOSI23.
* **Startup**: application ownership is constructed inside `setup()`. The blower is
  stopped, saved configuration is loaded, calibrated closure is commanded, and then
  tasks start. LEDC fan channel 0 and servo channel 2 use independent timers.

---

## 5. Building, Testing, and Simulation

### Repeatable software verification

From the repository root, install `uv`, a C++17 compiler, OpenSSL, Node/npm (for browser accessibility checks), and the runtime
libraries `libcjson.so.1` and `libmbedcrypto.so.7`. Build an ESP32 environment once to
install its framework headers, then run:

```bash
uv run --no-project --with platformio pio run -d firmware -e esp32dev
uv run --no-project --with playwright==1.58.0 python -m playwright install chromium
tools/verify.sh --firmware --browser
```

The driver runs Python tests, strict C++ domain/service/native checks, production
Arduino paths against SPI/PWM/NVS spies, the firmware HTTPS handler, both ESP32
converter builds, and the real simulator/browser flow. Omit `--firmware` or
`--browser` to skip those optional stages. Hardware spies do not establish electrical
accuracy or actual servo movement.

### Running Local Python Web Simulation
Launch the desktop simulator with the live web dashboard served at `https://127.0.0.1:8443`:

```bash
uv run python -m esp32_fan_control.Controller.cli --web --access-dir "$HOME/.local/share/esp32-smoker/device-a"
```

### Compiling Firmware for ESP32 Target
Using PlatformIO Core via `uv`:

```bash
cd firmware
uv run --with platformio pio run -e esp32dev
```

The default `esp32dev` environment supports one wired **K-type probe through a
MAX31855K** on CS5. For a MAX31856 configured as K-type, build
`-e esp32dev-max31856` and connect MOSI23 as well. A second wired food converter is
optional: add `-D SMOKER_FOOD_CS=21` to that environment's build flags when installed.
Both converters share SCK18/MISO19 and have separate CS pins. Never connect a bare
thermocouple directly to an ESP32 analog input.

### Flashing Firmware & LittleFS Web Assets to ESP32

First provision and copy the three private device files described in
[device access](../docs/device-access.md). Substitute the MAX31856 environment in
both upload commands when using that converter.
```bash
cd firmware

# 1. Upload program binary (compiled against custom 2.5MB app partition)
uv run --with platformio pio run -e esp32dev -t upload

# 2. Upload web dashboard files from firmware/data/ to LittleFS partition (1.44MB)
uv run --with platformio pio run -e esp32dev -t uploadfs

# 3. Open interactive serial console with automatic exception decoder
uv run --with platformio pio device monitor -b 115200
```

---

## 6. Hardware Flashing & Pre-Flight Verification Checklist

Before connecting high-draw actuators or attaching the unit to your smoker, complete this pre-flight verification sequence on the bench.

### 6.1 Pre-Power Multimeter & Pin Sanity Check

Perform these checks with the ESP32 **unpowered**:

- [ ] **Power Rail Isolation**: Check resistance between the 5V rail and GND using a multimeter. Ensure no short circuit exists ($R > 10\,\text{k}\Omega$).
- [ ] **Logic Level Isolation (3.3V vs 5V)**:
  - Verify MAX31855 VCC is wired strictly to ESP32 `3V3` (3.3V LDO pin), **never** to 5V.
  - Verify Blower Fan (+) and Servo (+) are connected to the 5V USB rail.
- [ ] **Flyback Diode Orientation**: Ensure the 1N5819 diode is placed directly across the fan terminals with the **cathode (silver band)** connected to +5V and **anode** connected to the MOSFET Drain (Fan -).
- [ ] **Gate Pull-Down**: Verify the $100\,\text{k}\Omega$ resistor is connected between `GPIO 25` (MOSFET Gate) and `GND`. This guarantees the blower fan remains completely off when the ESP32 is booting or in reset.
- [ ] **Brownout Buffer Capacitor**: Confirm the $1000\,\mu\text{F}$ electrolytic capacitor across the 5V bus is installed with correct polarity (negative stripe to GND).

### 6.2 Flashing & Initial Boot Verification

1. Connect the ESP32 DevKit to your computer via USB.
2. Flash the firmware binary and LittleFS filesystem:
   ```bash
   cd firmware
   uv run --with platformio pio run -e esp32dev -t upload
   uv run --with platformio pio run -e esp32dev -t uploadfs
   ```
3. Open the serial terminal:
   ```bash
   uv run --with platformio pio device monitor -b 115200
   ```
4. Verify startup behavior:
   - [ ] Fan remains off until a valid pit sample is available; servo closes at boot.
   - [ ] With provisioned access files, serial reports
     `[Web] Provisioned HTTPS dashboard ready on port 443.`
   - [ ] The private per-device SSID becomes visible; no password is printed.
   - [ ] No shared-bus or control-task initialization error is reported.
   - [ ] Periodic telemetry shows pit validity, demand, damper, fan, and lid status.

### 6.3 Functional Bench Calibration & Verification

Step through each subsystem to confirm physical hardware operation:

#### A. Thermocouple Subsystem (MAX31855K or MAX31856)
- [ ] **Ambient Sanity**: With thermocouple attached, verify serial console reports room temperature (~68°F – 74°F).
- [ ] **Thermal Response**: Pinch the tip of the thermocouple between your fingers; confirm temperature immediately rises to 80°F–88°F.
- [ ] **Open-Circuit Fault Detection**: Unplug or disconnect one thermocouple lead. Verify the serial log reports a pit fault and demand drops to `0.0%` (failsafe lock).

#### B. Damper Actuator Subsystem (MG90S Servo)
- [ ] **Zero-Position (Closed)**: At initial boot, confirm the damper rotor moves to fully closed position ($0^\circ$).
- [ ] **Idle-Detach Verification**: Observe the servo after it reaches position. Within 1.5 seconds, the ESP32 automatically detaches the PWM pin (`ledcDetachPin`). Confirm all servo hum, vibration, and jitter stops completely.
- [ ] **Full Range Sweep**: Adjust calibration in the dashboard and verify closed/open mechanical endpoints and direction. Confirm values persist across a reboot; verify movement is followed by idle detach.

#### C. Blower Fan Subsystem (N-MOSFET & 25kHz PWM)
- [ ] **Low-Demand Deadband**: When demand is below the airflow threshold (default $\le 40\%$), confirm the blower fan is completely idle (0% duty cycle) while the damper modulates airflow.
- [ ] **High-Demand Activation**: When demand exceeds the configured threshold (default 40%), confirm the blower fan spins up smoothly without any audible coil whine (verified 25 kHz ultrasonic PWM).
- [ ] **Max Speed (100% Demand)**: At 100% demand, confirm the fan runs at full 5V velocity.

#### D. Web Dashboard & Wi-Fi Connectivity
- [ ] Connect to the per-device Wi-Fi network listed in your private `operator-access.txt`.
- [ ] Open a browser and navigate to `https://192.168.4.1`.
- [ ] Verify the responsive pitmaster dashboard loads with real-time temperature gauges and canvas graph.
- [ ] Tap the **Setpoint** control, enter `250`, and submit. Verify:
  - Web UI displays new setpoint.
  - Dashboard reports application and successful persistence.
  - The value persists after power-cycling the ESP32 (stored in NVS flash).
- [ ] Tap **Lid Open Pause**. Verify status changes to `LID_OPEN`, blower turns off, and damper closes. Use the resume control to cancel.

#### E. Wireless BLE Meat Probe Verification (Optional / If Equipped)
- [ ] Turn on a compatible BLE meat probe (MEATER, Inkbird, or BBQ-BT).
- [ ] Within 10–30 seconds, confirm the serial log and web dashboard update the meat channel from `N/C` to the live wireless temperature reading with probe name and battery status (e.g. `Meat: 135.2 F [MEATER+ 95%]`).
- [ ] If a wired thermocouple is also plugged into the Food 1 channel, remove the wireless probe or move it out of range; verify the controller cleanly falls back to the wired probe reading without interrupting the pit regulation loop.

