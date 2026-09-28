# ESP32 Smoker Controller Firmware & Web Interface

Embedded C++17 firmware and offline-first web dashboard for the ESP32 Smoker Fan & Servo Damper Controller, structured using the PocketSWE Onion Architecture.

---

## 1. Directory Structure

```text
firmware/
├── platformio.ini              # PlatformIO build configuration (esp32dev / native)
├── partitions.csv              # Custom 4MB partition table (2.5MB App + 1.44MB LittleFS)
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
  "status": "REGULATING",
  "is_meat_wireless": true,
  "meat_battery_pct": 92,
  "meat_probe_name": "MEATER+"
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
  * Handles continuous asynchronous BLE background advertisements for wireless meat probes.
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
4. Verify the startup sequence matches the expected boot log:
   - [ ] Serial banner displays: `ESP32 Smoker Fan & Servo Damper Controller`
   - [ ] LittleFS mounts successfully: `[Web] LittleFS mounted successfully.`
   - [ ] SoftAP initializes: `[Web] Started SoftAP: SmokerController`, `[Web] AP IP address: 192.168.4.1`
   - [ ] Real-time control loop starts: `[Init] Real-time control loop running on FreeRTOS Core 1.`
   - [ ] Periodic 1Hz telemetry lines begin streaming:
     ```text
     [1000 ms] Pit: 72.5 F | Set: 225.0 F | Meat: N/C | Demand: 100.0% | Damper: 100.0% | Fan: 100.0% | Lid: CLOSED | State: REGULATING
     ```

### 6.3 Functional Bench Calibration & Verification

Step through each subsystem to confirm physical hardware operation:

#### A. Thermocouple Subsystem (MAX31855)
- [ ] **Ambient Sanity**: With thermocouple attached, verify serial console reports room temperature (~68°F – 74°F).
- [ ] **Thermal Response**: Pinch the tip of the thermocouple between your fingers; confirm temperature immediately rises to 80°F–88°F.
- [ ] **Open-Circuit Fault Detection**: Unplug or disconnect one thermocouple lead. Verify the serial log switches to `SENSOR_FAULT` and demand drops to `0.0%` (failsafe lock).

#### B. Damper Actuator Subsystem (MG90S Servo)
- [ ] **Zero-Position (Closed)**: At initial boot, confirm the damper rotor moves to fully closed position ($0^\circ$).
- [ ] **Idle-Detach Verification**: Observe the servo after it reaches position. Within 1.5 seconds, the ESP32 automatically detaches the PWM pin (`ledcDetachPin`). Confirm all servo hum, vibration, and jitter stops completely.
- [ ] **Full Range Sweep**: Send a test setpoint or adjust setpoint via API to demand 50% airflow; confirm damper opens to approximately $45^\circ$, then silences.

#### C. Blower Fan Subsystem (N-MOSFET & 25kHz PWM)
- [ ] **Low-Demand Deadband**: When demand is below the airflow threshold ($\le 50\%$), confirm the blower fan is completely idle (0% duty cycle) while the damper modulates airflow.
- [ ] **High-Demand Activation**: When demand exceeds 50%, confirm the blower fan spins up smoothly without any audible coil whine (verified 25 kHz ultrasonic PWM).
- [ ] **Max Speed (100% Demand)**: At 100% demand, confirm the fan runs at full 5V velocity.

#### D. Web Dashboard & Wi-Fi Connectivity
- [ ] Connect your phone or laptop to the Wi-Fi network `SmokerController` (password: `smoker123`).
- [ ] Open a browser and navigate to `http://192.168.4.1`.
- [ ] Verify the responsive pitmaster dashboard loads with real-time temperature gauges and canvas graph.
- [ ] Tap the **Setpoint** control, enter `250`, and submit. Verify:
  - Web UI displays new setpoint.
  - Serial monitor confirms setpoint update.
  - The value persists after power-cycling the ESP32 (stored in NVS flash).
- [ ] Tap **Lid Open Pause**. Verify status changes to `LID_OPEN`, blower turns off, and damper closes. Tap again to cancel.

#### E. Wireless BLE Meat Probe Verification (Optional / If Equipped)
- [ ] Turn on a compatible BLE meat probe (MEATER, Inkbird, or BBQ-BT).
- [ ] Within 10–30 seconds, confirm the serial log and web dashboard update the meat channel from `N/C` to the live wireless temperature reading with probe name and battery status (e.g. `Meat: 135.2 F [MEATER+ 95%]`).
- [ ] If a wired thermocouple is also plugged into the Food 1 channel, remove the wireless probe or move it out of range; verify the controller cleanly falls back to the wired probe reading without interrupting the pit regulation loop.

