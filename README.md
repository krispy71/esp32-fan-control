# esp32-fan-control

Smart temperature and airflow controller for charcoal and wood-based smokers built on the ESP32 microcontroller.

## Overview

The controller continuously reads temperature sensors in the smoke chamber and meat, orchestrating both a **5V forced-air blower fan** and a **servo-controlled mechanical damper** (inspired by the **HeaterMeter** and **Roto-Damper** / **MicroDamper** open-hardware projects).

### Key Features

* **Dual-Actuator Airflow Management**:
  * **Servo-Controlled Damper**: Prevents the convective "chimney draft effect" when the pit reaches target temperature, and provides fine-tuned natural draft regulation at low-and-slow cooking temperatures ($225^\circ\text{F}$).
  * **5V Radial Blower Fan**: Kicks in automatically above a configurable airflow threshold to deliver forced air during warm-up, setpoint recovery, or high-heat cooks.
  * **RJ45 Interface**: Uses the 8P8C pin assignments (Pins 3: +5V, 4: GND, 5: +5V Fan, 6: Servo Signal) with 5V damper/fan hardware. This project's USB power rail is 5V.
* **Multi-Probe Temperature Acquisition**:
  * Supports wired K-type thermocouple probes (via SPI MAX31855/MAX31856) for pit and food temperatures.
  * Supports wireless temperature probes (e.g. MEATER / BLE beacons) behind a pluggable sensor interface.
* **Autonomous Safety & Fail-Safes**:
  * All closed-loop regulation and safety logic reside strictly on the ESP32.
  * **Lid-Open Detection**: Detects rapid negative $\Delta T$ and automatically halts the fan and closes the damper to prevent firebox flare-ups.
  * **Fail-Safe Shutdown**: Automatically closes damper and stops fan on sensor disconnect.
* **Local Web Control Interface**:
  * Reachable over local Wi-Fi to monitor graphs, adjust setpoints, and tune PID/servo calibration parameters without requiring cloud services.

## Documentation

* [Project North Star](docs/north-star.md) — System requirements, behavior, and acceptance criteria.
* [System Architecture & Engineering Specification](docs/system-architecture.md) — Electrical schematic, pinout, circuit design, and modulation curves.
* [PocketSWE Agent Contract](AGENTS.md) — Onion architecture invariants and dependency rules.
