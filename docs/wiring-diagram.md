# ESP32 Smoker Controller — Wiring Guide & Electrical Schematic

> Complete electrical reference, point-to-point connection tables, breadboard guide, and off-the-shelf module recommendations.
> **No custom printed circuit board (PCB) is required.** The system is designed to be assembled using standard off-the-shelf breakout boards, breadboards, or perfboards.

---

## 1. Quick Answer: Custom Circuit Board vs. Breadboard

* **Is a custom PCB required?**  
  **No.** You do not need to design or order a custom PCB.
* **Can it be built on a solderless breadboard?**  
  **Yes.** The entire circuit can be assembled and fully bench-tested on a standard half-size or full-size solderless breadboard using DuPont jumper wires.
* **How to package for the 3D printed housing?**  
  For final installation inside the 3D-printed pod (`cad/smoker_damper.scad`), you can either:
  1. Use common pre-built breakout modules (e.g. a $1 MOSFET breakout module, a MAX31855 breakout board, and an ESP32 DevKit).
  2. Solder the few discrete components (MOSFET, diode, resistors, capacitor) onto a small $1 prototype perfboard / stripboard (approx. $30\text{mm} \times 50\text{mm}$).

---

## 2. Complete Schematic (Single 5V USB Rail)

```text
                                  +5V USB Power Source (Charger or Power Bank)
                                                |
          +-------------------------------------+---------------------------------------+
          |                                     |                                       |
          v                                     v                                       v
   [ 5V Blower Fan ]                     [ MG90S Servo ]                       [ ESP32 Dev Board ]
     (+) Red Lead                         (+) Red Lead                           (VIN / 5V Pin)
          |                                     |                                       |
     (-) Black Lead                             |                                       | (Internal LDO)
          |                                     |                                       v
          v Drain                               |                                  3.3V Bus
   +-------------+                              |                                       |
   |   N-MOSFET  |                              |                 +---------------------+---------------------+
   |  (AO3400A / |                              |                 |                     |                     |
   |   IRLZ44N)  |                              |                 v VCC                 v VCC                 |
   +------+------+                              |          [ MAX31855 Pit ]     [ MAX31855 Meat ]             |
   Gate | | Source                              |            Breakout              Breakout                   |
        | +-----------+                         |                 |                     |                     |
        |             |                         |                 |                     |                     |
        |            GND                        |                 |                     |                     |
        |             |                         |                 |                     |                     |
        |             +-------------------------+-----------------+---------------------+                     |
        |             |                         |                                                             |
        |             v (-)                     v (-)                                                         |
        |     +---------------+         +---------------+                                                     |
        |     | Flyback Diode |         | 1000uF Buffer |                                                     |
        |     | (1N5819 across|         | Electrolytic  |                                                     |
        |     | Fan + and -)  |         | Capacitor     |                                                     |
        |     +---------------+         +---------------+                                                     |
        |                                                                                                     |
        |                                                                                                     |
   150Ω | Gate Resistor                                                                                       |
   +----+--------------------------------- GPIO 25 (Blower PWM)                                               |
   |                                                                                                          |
 [100kΩ Pull-down to GND]                                                                                     |
                                          GPIO 26 (Servo PWM) ------------------------------------------------+
                                          (Direct to Servo Signal / Orange Wire)                              |
                                                                                                              |
                                          GPIO 18 (SPI SCK) ------------------ SCK (Shared) ------------------+
                                          GPIO 19 (SPI MISO) ----------------- SO (Shared) -------------------+
                                          GPIO 5  (CS Pit) ------------------- CS (Pit Module only)           |
                                          GPIO 21 (CS Meat) ---------------------------------- CS (Meat only) |
                                          GND -------------------------------- GND (Shared) ------------------+
```

---

## 3. Point-to-Point Pin Connection Table

### 3.1 Power Rail Connections (Single 5V USB Input)

| From (Source) | To (Destination) | Function | Notes |
| :--- | :--- | :--- | :--- |
| **USB 5V (+)** | ESP32 `VIN` / `5V` pin | System Logic Power | Powers ESP32 onboard 3.3V LDO regulator |
| **USB 5V (+)** | Blower Fan `(+)` Red Wire | Fan Power | Direct 5V power |
| **USB 5V (+)** | 1N5819 Diode Cathode (bar) | Inductive Snubber | Placed across fan terminals |
| **USB 5V (+)** | Servo Damper `(+)` Red Wire| Servo Power | 5V rail for MG90S |
| **USB 5V (+)** | 1000µF Cap `(+)` Lead | Brownout Buffer | Absorbs servo inrush currents |
| **USB GND (-)**| ESP32 `GND` pin | Common Ground | Reference ground |
| **USB GND (-)**| MOSFET `Source` Pin | Power Ground | Return path for fan |
| **USB GND (-)**| 100kΩ Resistor | Gate Pull-Down | Keeps fan OFF during boot |
| **USB GND (-)**| Servo Damper `(-)` Brown/Black Wire | Servo Ground | |
| **USB GND (-)**| 1000µF Cap `(-)` Lead | Capacitor Ground | |
| **USB GND (-)**| MAX31855 `GND` pins | Sensor Ground | Shared by both breakout boards |

---

### 3.2 Blower Fan Low-Side Switch Connections

| Component Pin | Connects To | Purpose |
| :--- | :--- | :--- |
| **ESP32 GPIO 25** | One side of $150\,\Omega$ resistor | LEDC PWM control output (25 kHz) |
| **$150\,\Omega$ resistor (other side)** | MOSFET `Gate` pin | Limits capacitive gate charge current |
| **$100\,\text{k}\Omega$ resistor** | Between MOSFET `Gate` and `GND` | Pull-down: prevents fan spinning during ESP32 boot |
| **MOSFET `Drain`** | Blower Fan `(-)` Black Wire + 1N5819 Anode | Switched return path |
| **MOSFET `Source`** | Common `GND` | Direct ground connection |
| **1N5819 Diode** | Across Fan `(+)` and `(-)` (Cathode to `+`, Anode to `-`) | Clamps inductive voltage spikes |

> **Pro Tip**: If using a pre-made MOSFET module (e.g. "LR7843 MOSFET module" or "IRF520 / D4184 module"), simply wire:
> - `SIG` to ESP32 `GPIO 25`
> - `GND` to ESP32 `GND`
> - `VIN + / -` to 5V power supply
> - `OUT + / -` to Blower Fan

---

### 3.3 Servo Damper Connections

| Component Pin | Connects To | Wire Color |
| :--- | :--- | :--- |
| **Servo Signal** | ESP32 `GPIO 26` | Orange wire (MG90S) / White wire |
| **Servo VCC (+)** | 5V USB Bus | Red wire |
| **Servo Ground (-)** | Common `GND` | Brown wire (MG90S) / Black wire |
| **1000µF 10V/16V Cap**| Across Servo VCC and GND | Electrolytic (Observe polarity: negative stripe to GND!) |

---

### 3.4 MAX31855 Thermocouple Breakout Connections

The MAX31855 runs on **3.3V logic**. Power both breakout modules from the ESP32 `3V3` pin.

| MAX31855 Pin | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **VCC** | ESP32 `3V3` pin | 3.3V Logic Power | Do **NOT** connect to 5V! |
| **GND** | ESP32 `GND` pin | Common Ground | Shared |
| **SCK** (Clock) | ESP32 `GPIO 18` | SPI Clock | Shared by Pit and Meat modules |
| **SO / DO** (Data Out) | ESP32 `GPIO 19` | SPI MISO | Shared by Pit and Meat modules |
| **CS (Pit Module)** | ESP32 `GPIO 5` | Pit Chip Select | Dedicated to Pit sensor |
| **CS (Meat Module)**| ESP32 `GPIO 21`| Meat Chip Select | Dedicated to Meat sensor |

---

## 4. Modular Tethered Setup: RJ45 Umbilical Pinout (Configuration B)

If you build **Configuration B** (microcontroller in a tabletop base unit with a Cat5/Cat6 network cable running to the damper pod on the smoker), wire the 8P8C (RJ45) jacks according to the community standard HeaterMeter pinout:

```text
                  RJ45 Female Socket (Front View / Tab Down)
                     +---+---+---+---+---+---+---+---+
                     | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
                     +---+---+---+---+---+---+---+---+
```

| RJ45 Pin | T568B Wire Color | Connected Net | Purpose |
| :---: | :--- | :--- | :--- |
| **1** | White / Orange | *Unused / NC* | Reserved |
| **2** | Orange | *Unused / NC* | Reserved |
| **3** | White / Green | **+5V Servo Power** | Carries 5V to MG90S servo |
| **4** | Blue | **Common GND** | Shared ground return for fan and servo |
| **5** | White / Blue | **+5V Switched Fan** | Switched 5V from MOSFET in base box to Blower |
| **6** | Green | **Servo Signal** | 50Hz PWM signal from ESP32 GPIO 26 |
| **7** | White / Brown | *Unused / NC* | Reserved |
| **8** | Brown | *Unused / NC* | Reserved |

---

## 5. Recommended Off-the-Shelf Parts (BOM)

| Component | Recommended Model | Approx. Cost | Notes |
| :--- | :--- | :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32D / DevKit V1 (30-pin or 38-pin) | ~$4.00 | Built-in Wi-Fi, Bluetooth, 3.3V LDO |
| **Chamber Probe Amp**| MAX31855 K-Type Thermocouple Breakout Board | ~$3.50 | SPI interface, cold-junction compensated |
| **Food Probe Amp** | MAX31855 Breakout Board (optional 2nd probe) | ~$3.50 | Shares SPI bus with pit probe |
| **Blower Fan** | 5V 5015 or 7530 DC Radial Blower Fan | ~$4.00 | Standard 50mm x 15mm or 75mm x 30mm centrifugal fan |
| **Damper Servo** | TowerPro MG90S (Metal Gear) Micro-Servo | ~$3.00 | High-torque metal gears withstand heat better than SG90 |
| **MOSFET Module** | LR7843, AO3400A, or IRLZ44N Breakout | ~$1.00 | Logic-level N-channel ($V_{GS} \le 3.3\text{V}$) |
| **Capacitor** | 1000µF 10V or 16V Radial Electrolytic Capacitor | ~$0.20 | Critical: suppresses servo stall brownouts |
| **Resistors** | $150\,\Omega$ and $100\,\text{k}\Omega$ 1/4W resistors | ~$0.10 | Gate damping and pull-down |
| **Diode** | 1N5819 Schottky or 1N4001 Rectifier Diode | ~$0.10 | Flyback protection |
| **Thermocouple** | Stainless braided K-type probe (pit and meat) | ~$4.00 | Rated up to 800°F (400°C) |

**Total Estimated Hardware Cost: ~$20.00 – $25.00**
