# ESP32 Smoker Controller Firmware

Embedded C++17 firmware for the ESP32 Smoker Fan & Servo Damper Controller, structured using the PocketSWE Onion Architecture.

---

## 1. Directory Structure

```text
firmware/
├── platformio.ini              # PlatformIO build configuration (esp32dev / native)
├── include/                    # Common project headers
├── src/
│   ├── Domain/                 # Pure domain business logic (no Arduino/ESP32 dependencies)
│   │   ├── Temperature.hpp     # TemperatureReading, SensorRole, SensorFault
│   │   ├── Airflow.hpp         # AirflowDemand, ActuatorTargets, ActuatorCoordinator
│   │   ├── PID.hpp             # Discrete PID regulator with anti-windup clamping
│   │   └── LidDetector.hpp     # Lid-open suppression state machine
│   ├── Services/               # Core application orchestration
│   │   ├── Ports/              # Abstract hardware contracts
│   │   │   ├── TemperatureSensorPort.hpp
│   │   │   ├── DamperActuatorPort.hpp
│   │   │   ├── BlowerActuatorPort.hpp
│   │   │   └── TelemetryPort.hpp
│   │   └── SmokerControlService.hpp
│   ├── Adapters/               # Concrete hardware drivers
│   │   ├── Actuators/
│   │   │   ├── ESP32ServoDamperAdapter.hpp # 50Hz LEDC PWM with idle-detach
│   │   │   └── ESP32PWMBlowerAdapter.hpp   # 25kHz ultrasonic MOSFET PWM
│   │   ├── Sensors/
│   │   │   └── MAX31855SensorAdapter.hpp   # SPI thermocouple acquisition & fault decoding
│   │   └── Telemetry/
│   │       └── SerialTelemetryAdapter.hpp  # Console/serial output formatter
│   └── Controller/
│       └── main.cpp            # FreeRTOS task setup & pin bootstrap
└── test/
    └── test_domain.cpp         # Host-executable C++17 unit tests
```

---

## 2. Hardware Pinout Reference

| Function | ESP32 GPIO | Peripheral / Mode | Notes |
| :--- | :---: | :--- | :--- |
| **Blower Fan Gate** | `GPIO 25` | LEDC Ch 0, 25 kHz PWM | Drives gate of N-MOSFET (AO3400A / IRLZ44N) |
| **Servo Damper Signal** | `GPIO 26` | LEDC Ch 1, 50 Hz PWM | Connects to RJ45 Pin 6 (MG90S micro-servo) |
| **SPI SCK** | `GPIO 18` | Hardware SPI Clock | Shared clock line for MAX31855 amplifiers |
| **SPI MISO** | `GPIO 19` | Hardware SPI Data | Shared serial data in from MAX31855 |
| **CS Pit Thermocouple** | `GPIO 5` | Active-low Chip Select | Dedicated CS for pit/chamber probe |
| **CS Food Thermocouple**| `GPIO 21` | Active-low Chip Select | Dedicated CS for meat probe 1 |

---

## 3. FreeRTOS Task Architecture

* **Core 1 — Control Loop Task (`control_loop_task`)**:
  * Pinned to CPU Core 1 with priority 2.
  * Runs at a strict, deterministic $1\,\text{Hz}$ rate via `vTaskDelayUntil`.
  * Samples MAX31855 thermocouple, checks sensor validity, evaluates lid-open state, executes PID calculation, translates demand to damper and fan via `ActuatorCoordinator`, commands hardware ports, and emits telemetry.
* **Core 0 — Housekeeping & Telemetry Task**:
  * Pinned to CPU Core 0.
  * Periodically updates `ESP32ServoDamperAdapter` to auto-detach the servo after 1.5s of stationary holding (eliminating servo hum and motor wear).
  * Handles Wi-Fi and WebSockets communications.

---

## 4. Building and Testing

### Running C++ Host Unit Tests (No hardware required)
The Domain and Services layers are pure C++17 with zero vendor dependencies, enabling instant host testing:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/src firmware/test/test_domain.cpp -o test_domain
./test_domain
```

### Compiling Firmware for ESP32 Target
Using PlatformIO Core via `uv`:

```bash
cd firmware
uv run --with platformio pio run -e esp32dev
```

### Flashing Firmware to Connected ESP32
```bash
cd firmware
uv run --with platformio pio run -e esp32dev -t upload
```
