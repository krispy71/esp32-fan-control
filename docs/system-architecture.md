# ESP32 Smoker Fan & Servo Damper Controller — System Architecture & Engineering Specification

> Comprehensive architectural breakdown and hardware/firmware specification for the ESP32-based Smoker Controller.

---

## 1. Hardware & Electrical Architecture

> See the full point-to-point pinout guide and schematic in [docs/wiring-diagram.md](wiring-diagram.md).

The system is designed around a single **5V DC power bus** sourced via USB-C or micro-USB, allowing direct operation from standard USB wall chargers ($5\text{V }2.0\text{A}\text{--}3.0\text{A}$) and portable USB power banks/battery bricks.

```text
                               +-----------------------------+
                               |     5V DC Power Input       |
                               |  (USB-C / USB Power Bank)   |
                               +--------------+--------------+
                                              |
                       +----------------------+----------------------+
                       |                                             |
                       v                                             v
       +-------------------------------+             +-------------------------------+
       |       5V Blower Rail          |             |     5V Servo & Logic Rail     |
       |  (Direct or via RJ45 Pin 5)   |             |    + LC Filter & 1000uF Cap   |
       +---------------+---------------+             +---------------+---------------+
                       |                                             |
                       |                             +---------------+---------------+
                       |                             |                               |
                       v                             v                               v
         +---------------------------+  +-------------------------+    +---------------------------+
         | Low-Side N-MOSFET Switch  |  |   5V Micro-Servo Rail   |    |    ESP32 Onboard LDO      |
         | (AO3400A / IRLZ44N)       |  |  (MG90S / SG90)         |    |     (5V -> 3.3V Rail)     |
         | + Flyback Diode (1N5819)  |  |   (Direct or RJ45 P3)   |    +-------------+-------------+
         +-------------^-------------+  +-------------------------+                  |
                       |                                                             v
                       | PWM Fan Gate (LEDC)                           +---------------------------+
                       |                                               |     ESP32 Dual-Core       |
                       +-----------------------------------------------+ GPIOs (3.3V Logic)       |
                                                                       |                           |
                                      50Hz Servo PWM Signal (LEDC)     | • Core 0: Wi-Fi / Web     |
                                  +------------------------------------+ • Core 1: Control Loop    |
                                  |                                    +-------------+-------------+
                                  v                                                  |
                     +--------------------------+                                    | SPI Bus
                     | 3.3V -> 5V Level Buffer  |                                    v
                     | (74AHCT1G125 or FET)     |                      +---------------------------+
                     +--------------------------+                      | Thermocouple ICs          |
                                                                       | (MAX31855 / MAX31856)     |
                                                                       +---------------------------+
```

### 1.1 Physical Packaging Configurations

The system supports two physical deployment models:

#### Configuration A (Primary — All-in-One Integrated Pod)
* **Description**: The 3D-printed housing holds the blower fan, servo damper, ESP32 microcontroller development board, thermocouple amplifier, and 5V USB-C power breakout in a single compact enclosure mounted directly to the smoker inlet vent.
* **Benefits**: Zero umbilical cables running between boxes; completely self-contained. The user connects only a USB-C power cable from a battery brick and thermocouple probe leads.
* **CAD Implementation**: OpenSCAD model `cad/smoker_damper.scad` with `include_electronics_bay = true`. Features internal cable raceways, USB-C socket cutout, thermocouple socket opening, and ventilated lid.

#### Configuration B (Secondary — Modular / Tethered Base Unit)
* **Description**: A remote base station (placed safely on a prep table) houses the ESP32, display, and probe inputs, connecting to a slim damper/fan head mounted on the smoker via an RJ45 umbilical cable.
* **Benefits**: Keeps the microcontroller, display, and electronics further away from smoker radiant heat and grease.
* **Interface**: Standard 8P8C (RJ45) jack conforming to the HeaterMeter community standard (standard T568B Cat5/Cat6 cabling):

| RJ45 Pin | T568B Wire Color | Function | Description |
| :---: | :---: | :---: | :--- |
| **1** | White / Orange | NC | Reserved / Unused |
| **2** | Orange | NC | Reserved / Unused |
| **3** | White / Green | **+5V Servo Power** | Regulated 5V rail for RC micro-servo |
| **4** | Blue | **Common Ground** | Shared ground for 5V Blower and 5V Servo |
| **5** | White / Blue | **+5V Blower Power** | Switched 5V rail for blower fan (or constant 5V with switched return) |
| **6** | Green | **Servo Signal** | 50Hz PWM control signal (5V buffered logic) |
| **7** | White / Brown | NC | Reserved / Unused |
| **8** | Brown | NC | Reserved / Unused |

### 1.2 Blower Fan Driver Circuit
* **Actuator**: 5V DC radial blower (e.g. 5015, 7530, or 9733 5V radial blower, typical current $0.2\text{A}\text{--}0.7\text{A}$).
* **Switching Element**: Low-side N-channel logic-level MOSFET (e.g. AO3400A or IRLZ44N) with low $R_{DS(\text{on})} < 0.03\,\Omega$ at $V_{GS} = 3.3\text{V}$.
* **Gate Drive**: Driven from an ESP32 LEDC PWM GPIO with a $150\,\Omega$ gate damping resistor and a $100\,\text{k}\Omega$ pull-down resistor to ground (holds gate LOW during boot/reset).
* **Snubber / Flyback**: 1N5819 Schottky diode across the blower terminals to absorb inductive flyback pulses during high-frequency PWM switching.

### 1.3 Servo Damper Driver Circuit
* **Actuator**: Micro-servo (TowerPro MG90S metal-gear or SG90).
* **Power & Filtering**: Powered directly by the 5V bus. Because micro-servos can draw stall current pulses upwards of $800\,\text{mA}$, a dedicated $1000\,\mu\text{F}$ low-ESR electrolytic capacitor and a $0.1\,\mu\text{F}$ ceramic capacitor are placed right at the servo power connection to prevent brownout glitches on the ESP32.
* **Signal Buffering**: 50Hz PWM pulse signal generated by ESP32 LEDC peripheral (500µs–2500µs pulse width) is buffered to 5V logic via a 74AHCT1G125 or small N-FET buffer to ensure clean edge transitions over longer cable runs.

### 1.4 Parametric OpenSCAD Mechanical Housing
The mechanical CAD design (`cad/smoker_damper.scad`) is fully parametric:
* **Fan Geometry**: Accommodates any blower form factor (`fan_size` 50mm, 75mm, 97mm; `fan_depth` 15mm, 30mm, 33mm).
* **Smoker Interface**: Modular nozzle adapter with both **round** (e.g. 1" or 1.25" NPT pipe) and **rectangular** (e.g. 50mm x 30mm sliding door) duct options and configurable mounting flange.
* **Electronics Enclosure**: Toggleable integrated electronics bay (Config A) vs slim RJ45 connector block (Config B).

---

## 2. Airflow Coordination & Control Theory

Smoker fires operate on fuel-rich combustion: the temperature is governed almost entirely by available oxygen.

```text
              Airflow Modulation Profile (Sequential / Dual-Stage)
              
    100% |                                      /----------------- Damper Position (100%)
         |                                     /
  Actuator|                                   /                  / Blower Speed (0 -> 100%)
  Output |                                  /                 /
         |                                 /                /
         |                                /               /
         |                               /              /
      0% |------------------------------+-------------+
         0%                       Threshold (e.g. 40%)      100%
                               Total Airflow Demand (PID Output)
```

### 2.1 The Chimney Effect & Damper Elimination
When a smoker is hot, hot air rising up through the exhaust stack creates a strong natural vacuum (the chimney effect). With a standard blower fan, this vacuum pulls fresh air directly through the stationary fan blades, causing the coals to stoke uncontrollably even with the fan at 0%.
- **Solution**: At $0\%$ demand, the servo closes the physical damper shutter to $0^\circ$ aperture, hermetically sealing the intake and stopping thermal runaway.

### 2.2 Dual-Stage Modulation ("On Above" Mode)
The PID algorithm outputs an abstract demand from $0.0$ to $1.0$ ($0\text{--}100\%$):
1. **Stage 1 — Natural Draft Regulation ($0\% < \text{Demand} \le \text{Threshold}$)**:
   - For typical low-and-slow cooks ($225^\circ\text{F}\text{--}250^\circ\text{F}$), forced air from a blower is often too aggressive, kicking up ash and overshooting target temps.
   - The damper aperture scales linearly from $0\%$ to $100\%$ while the **blower fan remains completely OFF**. Natural convective draft smoothly feeds the fire.
2. **Stage 2 — Forced Draft Boost ($\text{Demand} > \text{Threshold}$)**:
   - The damper remains locked at $100\%$ fully open.
   - The blower fan engages at its minimum speed and scales linearly up to $100\%$ duty cycle, providing forced air when large heat additions are required (e.g., startup, lid recovery, or high-heat grilling).

### 2.3 Servo Idle Detach
Holding a hobby servo at a stationary position under continuous 50Hz PWM pulses causes servo jitter, hum, and unnecessary power consumption.
- The firmware provides an auto-detach feature: once the servo reaches its target position, PWM pulses are disabled after 1.5 seconds of settling time. Pulses are re-enabled dynamically whenever the target position changes by $\ge 1\%$.

### 2.4 Lid-Open Suppression Algorithm
- The controller monitors the rolling rate of change of pit temperature: $\frac{dT_{\text{pit}}}{dt}$.
- If $\frac{dT_{\text{pit}}}{dt} < -\text{DropThreshold}$ (e.g., drops $>15^\circ\text{F}$ in under 30 seconds), the lid-open state is triggered.
- Fan is forced to $0\%$, damper is driven to $0\%$ (closed), and PID integration is paused.
- The suppression remains active for a configurable duration (default 180 seconds) or until temperature recovers, preventing the controller from blowing the firebox into an inferno while the pitmaster is spritzing or wrapping meat.

---

## 3. PocketSWE Onion Architecture Layout

The software is structured in accordance with PocketSWE and the [AGENTS.md](../AGENTS.md) contract:

```text
src/
├── Domain/
│   ├── airflow_demand.py / .hpp          # Typed airflow demand (0-100%) and actuator targets
│   ├── actuator_coordinator.py / .hpp    # Dual-stage mapping logic (Demand -> Damper % & Fan %)
│   ├── pid_regulator.py / .hpp           # PID algorithm with anti-windup & derivative filtering
│   ├── lid_open_detector.py / .hpp       # Temperature drop detection state machine
│   ├── temperature_reading.py / .hpp     # Measurement entity with timestamp and fault status
│   └── cook_profile.py / .hpp            # Setpoints, alarms, probe assignments
│
├── Services/
│   ├── smoker_control_service.py / .hpp  # Orchestrates read -> PID -> coordinate -> actuate cycle
│   ├── telemetry_service.py / .hpp       # Aggregates system metrics for reporting
│   └── cook_session_service.py / .hpp    # Manages cook timers, history, and setpoint updates
│   └── Ports/
│       ├── damper_actuator_port.py / .hpp # Contract: set_damper_position(pct)
│       ├── blower_actuator_port.py / .hpp # Contract: set_blower_speed(pct)
│       ├── temperature_sensor_port.py     # Contract: read_temperature() -> Reading
│       ├── telemetry_publisher_port.py    # Contract: publish(telemetry)
│       └── config_storage_port.py         # Contract: load/save settings
│
├── Adapters/
│   ├── Actuators/
│   │   ├── esp32_servo_damper_adapter.cpp # LEDC / MCPWM 50Hz pulse generation
│   │   └── esp32_pwm_blower_adapter.cpp   # LEDC MOSFET PWM modulation
│   ├── Sensors/
│   │   ├── max31855_thermocouple_adapter.cpp # SPI hardware driver
│   │   └── meater_ble_probe_adapter.cpp      # ESP32 BLE GATT client for wireless probes
│   ├── Network/
│   │   ├── web_server_adapter.cpp         # HTTP REST endpoints & WebSocket broadcaster
│   │   └── nvs_storage_adapter.cpp        # ESP32 Non-Volatile Storage (NVS) for config
│
└── Controller/
    └── main.cpp                          # FreeRTOS setup, task pinouts, dependency wiring
```

### 3.1 Inward Dependency Rules
- **Domain**: Pure business and control math. Contains no hardware timers, no ESP-IDF/Arduino includes, and no network logic. 100% testable on host/desktop environments.
- **Services**: Coordinate the domain models using Ports. Knows nothing about MOSFETs or servos—only `DamperActuatorPort` and `BlowerActuatorPort`.
- **Adapters**: Implement the ports and translate hardware nuances (e.g. converting 0-100% damper position into specific microsecond pulse timings).

---

## 4. Implementation Phasing Roadmap

- **Phase 1 — Core Dual-Actuator Firmware**:
  - Implement Domain PID and `ActuatorCoordinator` (dual-stage damper + blower logic).
  - Implement MAX31855 SPI thermocouple adapter and ESP32 LEDC servo/blower adapters.
  - Test on bench with RJ45-connected Roto-Damper / MicroDamper hardware.
- **Phase 2 — Pluggable Wireless Probe Adapters**:
  - Implement BLE scanner/client adapter for commercial wireless probes (MEATER / open BLE beacons).
  - Feed wireless readings through `TemperatureSensorPort` into the domain.
- **Phase 3 — Web Client Interface**:
  - Implement lightweight web UI (following [docs/ui-architecture.md](ui-architecture.md)) served over local Wi-Fi.
  - Expose live graphing of pit temp, meat temps, damper angle, and fan duty cycle.
