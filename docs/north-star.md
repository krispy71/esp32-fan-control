# ESP32 Smoker Fan & Damper Controller — Project North Star

Created: 2026-09-27. Status: working project direction; acceptance is not yet verified.

This document defines the system outcome for the ESP32 Smoker Controller. It follows the
[PocketSWE work protocol](work-protocol.md). Stated specifications represent target
operational envelopes to be verified through qualification tests.

---

## 1. Goal

Provide pitmasters with an autonomous, high-precision temperature regulator for charcoal and wood-fueled smokers. The controller continuously reads chamber (pit) and food (meat) temperatures, modulates active forced airflow via a 5V blower fan, and controls a physical servo-actuated damper to prevent convective chimney run-away and enable fine-grained natural draft regulation at low-and-slow temperatures.

The controller must operate safely and autonomously on the ESP32 microcontroller, remaining completely reliable regardless of Wi-Fi or browser connectivity.

---

## 2. Expected Behavior

1. **Power-On & Fail-Safe State**:
   - On boot, reset, or brownout, the blower fan PWM is immediately driven to 0% (OFF) and the servo damper is driven to 0% (fully CLOSED).
   - Sensor health and hardware peripherals are verified before enabling closed-loop regulation.

2. **Dual-Actuator Airflow Modulation (HeaterMeter & Roto-Damper Mode)**:
   - The PID control loop computes an aggregate **Airflow Demand** ($0\text{--}100\%$).
   - **Low Demand (Natural Draft Stage, $0\% \le \text{Demand} \le \text{Threshold}$)**:
     - The blower fan remains OFF ($0\%$).
     - The servo damper modulates smoothly between $0\%$ (closed) and $100\%$ (open).
     - For well-sealed smokers (e.g., Kamados, WSMs, drum cookers), this allows ultra-stable $225^\circ\text{F}$ holding without blowing ash or overshooting.
   - **High Demand (Forced Draft Stage, $\text{Demand} > \text{Threshold}$)**:
     - The servo damper remains fully OPEN ($100\%$).
     - The blower fan activates at its minimum operating speed and modulates linearly up to $100\%$ to aggressively stoke coals during warm-up, setpoint recovery, or high-temperature cooking.
   - **Zero Demand ($0\%$)**:
     - Both fan and damper are fully closed. The closed mechanical damper eliminates the "chimney draft effect" where hot exhaust drafts unmetered air through static fan blades.

3. **Lid Open Detection & Recovery**:
   - A rapid chamber temperature drop (configurable $-\Delta T / \Delta t$) indicates the smoker lid/door has been opened.
   - The controller immediately clamps airflow demand to $0\%$ (fan OFF, damper CLOSED) for a configurable pause duration (e.g. 180s) to prevent oxygen surges from stoking the fire into an uncontrolled blaze.
   - When the pause expires or temperature begins to stabilize, normal closed-loop regulation resumes with anti-windup protection.

4. **Multi-Probe Temperature Acquisition**:
   - Simultaneously reads chamber/pit temperature (for the control loop) and one or more meat/food probes (for cook target alerts).
   - Supports wired thermocouples (K-type via MAX31855/MAX31856 SPI converters) as the primary authoritative reference.
   - Supports pluggable wireless probe adapters (e.g. BLE probes like MEATER or custom RF beacons) behind a unified domain sensor contract.

5. **Local Network Connectivity & Telemetry**:
   - Broadcasts real-time telemetry (pit temp, setpoint, meat temps, fan speed %, damper %, PID components, mode) over local Wi-Fi.
   - Serves an embedded, responsive web control interface accessible on the local network (no cloud or internet access required).

---

## 3. Domain Concepts

- **Airflow Demand ($0\text{--}100\%$)**: The abstract output of the PID control loop representing the total volume of combustion air required.
- **Damper Position ($0\text{--}100\%$)**: The physical aperture percentage of the servo-driven shutter.
- **Blower Speed ($0\text{--}100\%$)**: The duty cycle of the forced-air blower fan.
- **Airflow Threshold ("On Above")**: The demand percentage (default 40–50%) above which the blower fan engages after the damper has reached full aperture.
- **Temperature Reading**: A validated measurement containing value ($^\circ\text{C}$ / $^\circ\text{F}$), sensor role (`Pit`, `Food1`, `Food2`), timestamp/freshness, and diagnostic fault state (`Ok`, `Disconnected`, `ShortToVcc`, `ShortToGnd`, `Stale`).
- **Damper Calibration**: Pulse-width bounds in microseconds ($\text{Pulse}_{\text{close\_us}}$, $\text{Pulse}_{\text{open\_us}}$) and direction inversion flag to accommodate various 3D-printed damper mechanics.
- **Lid Open State**: Temporal state where control output is inhibited due to detected ambient air inrush.

---

## 4. Invariants

- **Autonomous Device Safety**: The ESP32 control loop and fault policies must never depend on Wi-Fi, WebSocket, or browser connection status. If network drops or the web client disconnects, temperature regulation and safety limits continue uninterrupted.
- **Fail-Safe Actuator Shutdown**: Any persistent pit probe fault (disconnection or out-of-range reading) must immediately force fan output to $0\%$ and close the servo damper to starve the fire.
- **Anti-Windup Integration**: The integral term of the PID loop must be clamped during saturation, during lid-open suppression, and when actuator limits are reached.
- **No Direct Hardware in Domain**: Domain calculation of PID, damper/fan coordination, and lid-open state machines must contain zero GPIO, timer, or vendor library calls.
- **Aperture-Before-Forced-Air**: Under no normal operating condition shall the blower fan run while the servo damper is closed. The damper must lead or match blower activation.

---

## 5. Constraints

- **Supply Voltage & Power Compatibility**: Standard **5V DC input** via USB-C or micro-USB ($2.0\text{A}\text{--}3.0\text{A}$ capacity), enabling the system to run on standard phone chargers and USB battery bricks / power banks for outdoor field use. The 5V rail directly powers the 5V blower fan, 5V micro-servo, and feeds the ESP32 onboard 3.3V LDO.
- **Servo Motor Power Isolation & Decoupling**: The 5V micro-servo (MG90S) draws peak current spikes up to $800\text{mA}$ during movement. The 5V bus features bulk decoupling capacitance ($1000\,\mu\text{F}$) and filtering to isolate motor transients from the ESP32.
- **Physical Packaging Form Factors**:
  - **Configuration A (Primary — All-in-One Integrated Pod)**: The 3D-printed housing integrates the blower, servo damper, ESP32 microcontroller, 5V power input, and thermocouple amplifier into a single unit mounted directly to the smoker inlet vent.
  - **Configuration B (Secondary — Modular / Tethered Base Unit)**: A tabletop base unit holds the ESP32 and probe connectors, linking to a slim fan/damper pod on the smoker via an RJ45 umbilical cable utilizing the 4-wire community standard pinout (Pins 3: +5V, 4: GND, 5: +5V Fan, 6: Servo Signal).
- **Parametric OpenSCAD Mechanical Design**: The housing and output nozzle are defined parametrically in OpenSCAD (`cad/smoker_damper.scad`). The fan size (diameter and thickness: 5015, 7530, 9733) and the smoker connection nozzle (round pipe diameter or rectangular duct dimensions) are configurable to adapt to any smoker model (Kamado, WSM, drum, offset).
- **Microcontroller**: Espressif ESP32 dual-core processor using FreeRTOS tasks (Core 0: Network/Web services, Core 1: Deterministic sensor sampling & PID control loop).

---

## 6. Non-Goals

- Direct AC mains heater control (this project is exclusively for charcoal/wood smoker airflow regulation).
- Proprietary cloud-only service lock-in; the device will not require internet access to function.
- Designing custom silicon for wireless probes in Phase 1 (existing wired thermocouples and commercial BLE probes will be integrated first).
- Requiring dedicated 12V automotive batteries or proprietary wall-warts (5V USB power is the target power envelope).

---

## 7. Acceptance Criteria & Evidence Gates

1. **Gate 1 — Actuator Driver & RJ45 Compatibility**:
   - Driven via standard RJ45 port with verified HeaterMeter pinout.
   - Servo moves from 0% (closed, ~1000µs) to 100% (open, ~2000µs) at 50Hz without jitter.
   - Blower fan modulates cleanly from 0% to 100% PWM via low-side MOSFET without back-EMF reset of ESP32.
2. **Gate 2 — Coordinated Airflow Execution**:
   - At $0\%$ demand: Damper closed, fan OFF.
   - At $25\%$ demand (below threshold): Damper at $50\%$, fan OFF.
   - At $50\%$ demand (threshold): Damper at $100\%$, fan OFF.
   - At $75\%$ demand: Damper at $100\%$, fan at $50\%$.
   - At $100\%$ demand: Damper at $100\%$, fan at $100\%$.
3. **Gate 3 — Closed-Loop Smoker Regulation**:
   - Holds pit temperature within $\pm 2^\circ\text{F}$ of setpoint at steady state on a sealed charcoal smoker.
   - Lid open test: temperature drop of $>15^\circ\text{F}$ in $<30\text{s}$ triggers lid-open mode, shutting fan and damper.
4. **Gate 4 — Probe Fault Handling**:
   - Disconnecting the pit thermocouple immediately engages fail-safe clamp (damper closed, fan off) within 1 sample cycle.
5. **Gate 5 — Local Web Telemetry & Control**:
   - Web interface displays live pit/meat temps and damper/fan status updated at $\ge 1\text{Hz}$ over local Wi-Fi.
   - Setpoints and calibration limits can be modified via web UI and persist across reboot in non-volatile storage.
