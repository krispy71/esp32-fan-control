# ESP32 Smoker Controller Firmware & Web Interface

Embedded C++17 firmware and offline-first web dashboard for the ESP32 Smoker Fan & Servo Damper Controller, structured using the PocketSWE Onion Architecture.

---

## 1. Directory Structure

```text
firmware/
├── platformio.ini              # PlatformIO build configuration (esp32dev / native)
├── include/                    # Common project headers
├── data/                       # LittleFS web dashboard static assets
│   ├── index.html              # Responsive pitmaster control dashboard
│   ├── style.css               # Dark theme responsive mobile-first stylesheet
│   └── app.js                  # SSE/polling telemetry client & HTML5 canvas trend graph
├── src/
│   ├── Domain/                 # Pure domain business logic (zero Arduino/ESP32 dependencies)
│   │   ├── Temperature.hpp     # TemperatureReading, SensorRole, SensorFault
│   │   ├── Airflow.hpp         # AirflowDemand, ActuatorTargets, ActuatorCoordinator
│   │   ├── Configuration.hpp   # SmokerConfig domain model and validation invariants
│   │   ├── PID.hpp             # Discrete PID regulator with anti-windup clamping
│   │   └── LidDetector.hpp     # Lid-open suppression state machine
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
│   │   │   └── MAX31855SensorAdapter.hpp   # SPI thermocouple acquisition & fault decoding
│   │   ├── Telemetry/
│   │   │   └── SerialTelemetryAdapter.hpp  # Console/serial output formatter
│   │   ├── Storage/
│   │   │   └── ESP32NVSConfigAdapter.hpp   # Non-Volatile Storage (NVS) Preferences adapter
│   │   └── Network/
│   │       └── WebServerAdapter.hpp        # HTTP REST endpoints & LittleFS file server
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
| **Servo Damper Signal** | `GPIO 26` | LEDC Ch 1, 50 Hz PWM | Connects to RJ45 Pin 6 (MG90S micro-servo) |
| **SPI SCK** | `GPIO 18` | Hardware SPI Clock | Shared clock line for MAX31855 amplifiers |
| **SPI MISO** | `GPIO 19` | Hardware SPI Data | Shared serial data in from MAX31855 |
| **CS Pit Thermocouple** | `GPIO 5` | Active-low Chip Select | Dedicated CS for pit/chamber probe |
| **CS Food Thermocouple**| `GPIO 21` | Active-low Chip Select | Dedicated CS for meat probe 1 |

---

## 3. Web Dashboard & REST API Endpoints

The web dashboard is served directly from ESP32 LittleFS flash storage over SoftAP (`SSID: SmokerController`, `Pass: smoker123`) or local Wi-Fi.

### HTTP Endpoints

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/` | Serves dashboard HTML (`index.html`) |
| `GET` | `/style.css` | Serves dark theme CSS |
| `GET` | `/app.js` | Serves real-time JavaScript frontend |
| `GET` | `/api/telemetry` | Returns instantaneous JSON state snapshot |
| `GET` | `/api/events` | Server-Sent Events (SSE) telemetry stream at 1Hz |
| `GET` | `/api/config` | Read persisted tuning config ($K_p, K_i, K_d$, thresholds) |
| `POST` | `/api/setpoint` | Update pit setpoint: `{"setpoint": 225.0}` (auto-saved to NVS) |
| `POST` | `/api/config` | Update tuning parameters and persist to NVS flash |
| `POST` | `/api/lid-pause` | Trigger or toggle manual lid-opening airflow suppression |

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
  "status": "REGULATING"
}
```

---

## 4. FreeRTOS Task Architecture

* **Core 1 — Control Loop Task (`control_loop_task`)**:
  * Pinned to CPU Core 1 with priority 2.
  * Runs at a strict, deterministic $1\,\text{Hz}$ rate via `vTaskDelayUntil`.
  * Samples MAX31855 thermocouple, checks sensor validity, evaluates lid-open state, executes PID calculation, translates demand to damper and fan via `ActuatorCoordinator`, commands hardware ports, and emits telemetry.
* **Core 0 — Housekeeping & Web Server**:
  * Pinned to CPU Core 0.
  * Serves HTTP REST endpoints and web dashboard assets via `WebServerAdapter`.
  * Periodically updates `ESP32ServoDamperAdapter` to auto-detach the servo after 1.5s of stationary holding (eliminating servo hum, jitter, and motor wear).

---

## 5. Building, Testing, and Simulation

### Running C++ Host Unit Tests (No hardware required)
The Domain and Services layers are pure C++17 with zero vendor dependencies, enabling instant host testing:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/src firmware/test/test_domain.cpp -o test_domain
./test_domain
```

### Running Local Python Web Simulation
Launch the desktop simulator with the live web dashboard served at `http://127.0.0.1:8080`:

```bash
uv run python -m esp32_fan_control.Controller.cli --web
```

### Compiling Firmware for ESP32 Target
Using PlatformIO Core via `uv`:

```bash
cd firmware
uv run --with platformio pio run -e esp32dev
```

### Flashing Firmware & LittleFS Web Assets to ESP32
```bash
cd firmware
# 1. Upload program binary
uv run --with platformio pio run -e esp32dev -t upload

# 2. Upload web dashboard files from firmware/data/ to LittleFS
uv run --with platformio pio run -e esp32dev -t uploadfs
```
