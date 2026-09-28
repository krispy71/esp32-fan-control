# 3D Printed Parts Assembly Guide

> Mechanical assembly instructions and exploded parts diagram for the ESP32 Smoker Controller and Airflow Damper System.

---

## 1. Exploded Assembly Diagram

The technical diagram below illustrates how each 3D-printed component and off-the-shelf electro-mechanical part fits together:

![Exploded Assembly Diagram](images/parts_assembly_diagram.jpg)

---

## 2. Parts Index & Bill of Materials

### 2.1 3D Printed Components (Located in [`cad/stl/`](../cad/stl/))

| Exact STL Filename | Description | Quantity | Material Recommendation |
| :--- | :--- | :---: | :--- |
| [`smoker_housing.stl`](../cad/stl/smoker_housing.stl) | Central chassis body containing the 5015 blower pocket, lateral through-bore damper sleeve, horizontal MG90S servo bracket, and electronics bay. | 1 | PETG / ABS / ASA |
| [`damper_rotor.stl`](../cad/stl/damper_rotor.stl) | Cylindrical rotating barrel valve (27.1mm OD x 33.9mm L) with knurled left retaining bezel, 20x15mm airflow aperture, internal servo horn pocket, and 5.5mm axial screwdriver tunnel. | 1 | PETG / ABS / ASA |
| [`bbq_guru_vision_pro_nozzle_adapter.stl`](../cad/stl/bbq_guru_vision_pro_nozzle_adapter.stl) | 31.5mm OD cylindrical output nozzle featuring Dash-121/122 O-ring groove, 38mm stop collar, and 4-bolt mounting flange. | 1 | PETG / ABS / ASA |
| [`fan_cover.stl`](../cad/stl/fan_cover.stl) | Intake protection grille with concentric flow rings and crossbars to prevent finger/debris contact with blower impeller. | 1 | PETG / PLA / ABS |
| [`electronics_lid.stl`](../cad/stl/electronics_lid.stl) | Vented snap/screw lid protecting the ESP32, MAX31855, and MOSFET circuit with heat exhaust louvers. | 1 | PETG / PLA / ABS |
| [`vision_pro_kamado_slide_plate.stl`](../cad/stl/vision_pro_kamado_slide_plate.stl) | Direct-fit curved slide plate ($R = 195\,\text{mm}$) with 31.8mm (1-1/4") female receiver port for Vision Kamado Pro S-Series draft doors. | 1 | PETG / ABS / ASA |

### 2.2 Commercial Off-the-Shelf (COTS) Hardware & Fasteners

| Hardware Item | Purpose | Quantity |
| :--- | :--- | :---: |
| **5015 DC Radial Blower Fan (5V)** | Centrifugal combustion air induction fan ($50\text{mm} \times 15\text{mm}$) | 1 |
| **TowerPro MG90S Micro-Servo** | Metal-gear servo motor driving the rotating damper rotor | 1 |
| **Dash-121 or Dash-122 Silicone O-Ring** | High-temp friction seal on nozzle adapter ($28.3\text{mm} \text{ ID} \times 2.6\text{mm} \text{ CS}$) | 1 |
| **M3 x 20mm Socket Head Screws** | Secures `fan_cover.stl` and 5015 blower fan to housing body | 3 |
| **M3 x 10mm Socket Head Screws** | Secures `bbq_guru_vision_pro_nozzle_adapter.stl` to front housing boss | 4 |
| **M2 x 8mm Self-Tapping Screws** | Secures MG90S servo mounting ears to bracket tower | 2 |
| **M3 x 6mm Button Head Screws** | Secures `electronics_lid.stl` to electronics bay corner posts | 2 |
| **Servo Arm / Horn Screw** | Secures horn to servo brass output spline (included with MG90S) | 1 |

---

## 3. Step-by-Step Assembly Procedure

### Step 1: Damper Rotor & Servo Subassembly
1. Power the ESP32 or servo tester on the bench to zero the MG90S servo position ($0^\circ = \text{closed}$ damper aperture).
2. Insert the MG90S servo horizontally into the servo bracket cavity on the right side of [`smoker_housing.stl`](../cad/stl/smoker_housing.stl) with its output spline passing through the 12.5mm dividing wall opening into the damper barrel. Secure the servo ears with two M2x8mm screws.
3. Take the single-arm servo horn provided with the MG90S and press-fit it into the drive-end pocket of [`damper_rotor.stl`](../cad/stl/damper_rotor.stl).
4. Slide [`damper_rotor.stl`](../cad/stl/damper_rotor.stl) directly into the open cylindrical bore from the **left face** of [`smoker_housing.stl`](../cad/stl/smoker_housing.stl) until the rotor horn engages the servo spline and the knurled retaining bezel rests flush against the sleeve face.
5. Insert a magnetic M2 screwdriver through the 5.5mm central axial tunnel in the rotor's left knurled flange and tighten the servo center screw directly into the brass spline.
6. Verify smooth $90^\circ$ rotation:
   * **$0^\circ$ (Closed)**: Damper barrel solid wall blocks the airway completely (anti-chimney draft). Pointer indicates Closed.
   * **$90^\circ$ (Open)**: Rectangular cross-bore aligns 100% with the 20x15mm airway channel.

### Step 2: Blower Fan & Intake Grille Installation
1. Route the 5015 blower fan red (+) and black (-) wires through the wiring pass-through slot into the electronics bay.
2. Drop the 5015 blower fan into the square pocket on [`smoker_housing.stl`](../cad/stl/smoker_housing.stl) with the rectangular exhaust throat pointing toward the damper transition duct.
3. Place [`fan_cover.stl`](../cad/stl/fan_cover.stl) over the fan intake.
4. Fasten the cover and fan to the housing using three M3x20mm screws into the pilot holes. Do not overtighten.

### Step 3: Nozzle Adapter & O-Ring Fitment
1. Stretch the high-temperature silicone O-Ring (Dash-121 or Dash-122) over the tip of [`bbq_guru_vision_pro_nozzle_adapter.stl`](../cad/stl/bbq_guru_vision_pro_nozzle_adapter.stl) and seat it in the retention groove ($10\,\text{mm}$ back from the nozzle tip).
2. Align the nozzle adapter's 4 mounting holes with the threaded boss on the front of [`smoker_housing.stl`](../cad/stl/smoker_housing.stl).
3. Secure the adapter plate using four M3x10mm screws.

### Step 4: Electronics Installation & Lid Closure
1. Place the ESP32 DevKit, MAX31855 thermocouple amplifiers, and MOSFET module inside the rear bay of [`smoker_housing.stl`](../cad/stl/smoker_housing.stl) according to [docs/wiring-diagram.md](wiring-diagram.md).
2. Connect the servo signal lead (Orange) to `GPIO 26` and blower fan gate lead to `GPIO 25`.
3. Route the USB-C / micro-USB power cable through the side wall notch.
4. Align [`electronics_lid.stl`](../cad/stl/electronics_lid.stl) over the bay and press until the inner centering lip seats inside the perimeter. Secure with two M3x6mm screws.

### Step 5: Kamado Grill Mounting
1. Remove the factory bottom draft door slide on your Vision Pro Kamado grill.
2. Slide [`vision_pro_kamado_slide_plate.stl`](../cad/stl/vision_pro_kamado_slide_plate.stl) directly into the curved bottom track until fully seated.
3. Insert the assembled controller pod's 31.5mm nozzle into the 31.8mm female receiver port on the slide plate.
4. Push firmly until the 38mm stop collar rests flush against the receiver tube face. The silicone O-ring provides an airtight, vibration-damped friction fit.
