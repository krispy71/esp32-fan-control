#!/usr/bin/env python3
"""
ESP32 Smoker Controller — Hobbyist Green Perfboard Circuit Layout Generator
Renders a high-resolution technical CAD diagram of the 40mm x 60mm (14x22 pad)
double-sided prototyping circuit board.
"""

import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from PIL import Image

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_IMG_DIR = os.path.join(REPO_ROOT, "docs", "images")
os.makedirs(DOCS_IMG_DIR, exist_ok=True)

# Grid specifications
COLS = 14  # A through N (X = 1..14)
ROWS = 22  # 1 through 22 (Y = 1..22)
COL_LABELS = [chr(65 + i) for i in range(COLS)] # A, B, C, ... N

def draw_perfboard_base(ax, title, subtitle=""):
    """Draws the realistic green FR4 perfboard background with plated pads and silk labels."""
    margin_x = 0.8
    margin_y = 0.8
    width = COLS + 2 * margin_x - 1
    height = ROWS + 2 * margin_y - 1

    # Outer FR4 Board outline with rounded corners
    board_rect = patches.FancyBboxPatch(
        (1 - margin_x, 1 - margin_y), width, height,
        boxstyle="round,pad=0.2,rounding_size=0.6",
        facecolor="#143825", edgecolor="#0b2316", linewidth=2.5, zorder=1
    )
    ax.add_patch(board_rect)

    # Inner silkscreen perimeter border
    border_rect = patches.FancyBboxPatch(
        (1 - margin_x + 0.3, 1 - margin_y + 0.3), width - 0.6, height - 0.6,
        boxstyle="round,pad=0.1,rounding_size=0.4",
        facecolor="none", edgecolor="#e2e8f0", linewidth=1.0, alpha=0.60, zorder=2
    )
    ax.add_patch(border_rect)

    # Corner mounting holes (standard 3.2mm holes)
    corner_holes = [(1.1, 1.1), (COLS - 0.1, 1.1), (1.1, ROWS - 0.1), (COLS - 0.1, ROWS - 0.1)]
    for chx, chy in corner_holes:
        ax.add_patch(patches.Circle((chx, chy), 0.58, facecolor="#09130c", edgecolor="#c5a059", lw=1.6, zorder=4))

    # Grid pads
    for x in range(1, COLS + 1):
        for y in range(1, ROWS + 1):
            if (x in [1, COLS]) and (y in [1, ROWS]):
                continue
            # Plated copper annular ring
            ax.add_patch(patches.Circle((x, y), 0.35, facecolor="#d4af37", edgecolor="#b89328", lw=0.6, zorder=3))
            # Center drill hole
            ax.add_patch(patches.Circle((x, y), 0.16, facecolor="#09130c", edgecolor="none", zorder=4))

    # Silkscreen Row & Column Labels
    for i, x in enumerate(range(1, COLS + 1)):
        lbl = COL_LABELS[i]
        ax.text(x, 0.18, f"{lbl}", color="#f8fafc", fontsize=8.0, fontweight='bold', ha='center', va='center', zorder=5)
        ax.text(x, ROWS + 0.82, f"{lbl}", color="#f8fafc", fontsize=8.0, fontweight='bold', ha='center', va='center', zorder=5)

    for y in range(1, ROWS + 1):
        ax.text(0.12, y, f"{y}", color="#f8fafc", fontsize=7.2, ha='center', va='center', zorder=5)
        ax.text(COLS + 0.88, y, f"{y}", color="#f8fafc", fontsize=7.2, ha='center', va='center', zorder=5)

    # Subtitle
    ax.set_title(title, fontsize=12.5, fontweight='bold', pad=14, color="#0f172a")
    if subtitle:
        ax.text((COLS + 1) / 2.0, -1.1, subtitle, fontsize=8.5, color="#475569", ha='center', va='bottom', style='italic')

    ax.set_xlim(-2.8, COLS + 3.8)
    ax.set_ylim(ROWS + 1.8, -1.8) # Invert Y so Row 1 is at top
    ax.set_aspect('equal')
    ax.axis('off')

def draw_top_components(ax):
    """Draws component footprints and top-side wiring on the left panel."""
    # 1. 5V Power Input Screw Terminal (2-pin 5.08mm pitch: Pins at (2,1) and (4,1))
    term_pwr = patches.Rectangle((1.3, 0.4), 3.4, 1.2, facecolor="#0284c7", edgecolor="#0369a1", lw=1.5, zorder=10)
    ax.add_patch(term_pwr)
    ax.add_patch(patches.Circle((2, 1), 0.32, facecolor="#fef08a", edgecolor="#ca8a04", lw=1.2, zorder=11))
    ax.add_patch(patches.Circle((4, 1), 0.32, facecolor="#fef08a", edgecolor="#ca8a04", lw=1.2, zorder=11))
    ax.text(2, -0.6, "+5V IN", color="#dc2626", fontsize=7.0, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(4, -0.6, "GND IN", color="#1e293b", fontsize=7.0, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(3, 1.0, "5V PWR", color="#ffffff", fontsize=6.5, fontweight='bold', ha='center', va='center', zorder=12)

    # 2. Blower Fan Output Screw Terminal (2-pin 5.08mm pitch: Pins at (6,1) and (8,1))
    term_fan = patches.Rectangle((5.3, 0.4), 3.4, 1.2, facecolor="#0284c7", edgecolor="#0369a1", lw=1.5, zorder=10)
    ax.add_patch(term_fan)
    ax.add_patch(patches.Circle((6, 1), 0.32, facecolor="#fef08a", edgecolor="#ca8a04", lw=1.2, zorder=11))
    ax.add_patch(patches.Circle((8, 1), 0.32, facecolor="#fef08a", edgecolor="#ca8a04", lw=1.2, zorder=11))
    ax.text(6, -0.6, "FAN (+)", color="#dc2626", fontsize=7.0, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(8, -0.6, "FAN (-)", color="#0284c7", fontsize=7.0, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(7, 1.0, "FAN OUT", color="#ffffff", fontsize=6.5, fontweight='bold', ha='center', va='center', zorder=12)

    # 3. Servo Damper 3-Pin Male Header (Pins at (11,1), (12,1), (13,1))
    header_servo = patches.Rectangle((10.6, 0.6), 2.8, 0.8, facecolor="#1e293b", edgecolor="#475569", lw=1.0, zorder=10)
    ax.add_patch(header_servo)
    for px in [11, 12, 13]:
        ax.add_patch(patches.Rectangle((px - 0.16, 1 - 0.16), 0.32, 0.32, facecolor="#fef08a", edgecolor="#ca8a04", lw=0.8, zorder=11))
    ax.text(11, -0.6, "GND", color="#1e293b", fontsize=6.5, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(12, -0.6, "+5V", color="#dc2626", fontsize=6.5, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(13, -0.6, "SIG", color="#ea580c", fontsize=6.5, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(12, 1.7, "SERVO", color="#cbd5e1", fontsize=6.0, fontweight='bold', ha='center', va='top', zorder=12)

    # 4. 1000uF 16V Buffer Capacitor (Radial can at (11.5, 3.2))
    cap_circle = patches.Circle((11.5, 3.2), 0.95, facecolor="#1e3a8a", edgecolor="#93c5fd", lw=1.5, zorder=12)
    ax.add_patch(cap_circle)
    # Negative polarity stripe on can (pin 11)
    ax.add_patch(patches.Wedge((11.5, 3.2), 0.95, 120, 240, facecolor="#e2e8f0", edgecolor="none", zorder=13))
    ax.text(11.0, 3.2, "-", color="#1e293b", fontsize=10, fontweight='bold', ha='center', va='center', zorder=14)
    ax.text(12.0, 3.2, "+", color="#ffffff", fontsize=9, fontweight='bold', ha='center', va='center', zorder=14)
    ax.text(11.5, 4.4, "1000µF 16V", color="#bfdbfe", fontsize=6.5, fontweight='bold', ha='center', va='top', zorder=15)

    # 5. 1N5819 Schottky Flyback Diode across Fan terminals (Cathode at (6,2), Anode at (8,2))
    ax.plot([6, 8], [2, 2], color="#94a3b8", lw=2.0, zorder=10)
    diode_body = patches.Rectangle((6.5, 1.78), 1.0, 0.44, facecolor="#0f172a", edgecolor="#475569", lw=0.8, zorder=11)
    ax.add_patch(diode_body)
    # Cathode silver stripe (near Pin 6)
    diode_band = patches.Rectangle((6.65, 1.78), 0.18, 0.44, facecolor="#e2e8f0", edgecolor="none", zorder=12)
    ax.add_patch(diode_band)
    ax.text(7.0, 2.4, "1N5819", color="#cbd5e1", fontsize=6.5, fontweight='bold', ha='center', va='top', zorder=15)

    # 6. TO-220 N-MOSFET (Pins at (7,4)=Gate, (8,4)=Drain, (9,4)=Source)
    mosfet_body = patches.Rectangle((6.5, 3.4), 3.0, 0.9, facecolor="#1e293b", edgecolor="#64748b", lw=1.2, zorder=10)
    ax.add_patch(mosfet_body)
    mosfet_tab = patches.Rectangle((6.5, 4.2), 3.0, 0.25, facecolor="#cbd5e1", edgecolor="#94a3b8", lw=0.8, zorder=11)
    ax.add_patch(mosfet_tab)
    ax.text(7, 3.2, "G", color="#38bdf8", fontsize=6.5, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(8, 3.2, "D", color="#f87171", fontsize=6.5, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(9, 3.2, "S", color="#4ade80", fontsize=6.5, fontweight='bold', ha='center', va='bottom', zorder=15)
    ax.text(8, 3.8, "IRLZ44N", color="#f8fafc", fontsize=6.5, fontweight='bold', ha='center', va='center', zorder=12)

    # 7. 100k Gate Pull-Down Resistor (Between (7,5) and (9,5))
    ax.plot([7, 9], [5, 5], color="#94a3b8", lw=1.8, zorder=10)
    r100k = patches.Rectangle((7.4, 4.82), 1.2, 0.36, facecolor="#dbeafe", edgecolor="#60a5fa", lw=0.8, zorder=11)
    ax.add_patch(r100k)
    ax.text(8, 5.0, "100kΩ", color="#1e3a8a", fontsize=6.0, fontweight='bold', ha='center', va='center', zorder=12)

    # 8. 150 Ohm Gate Resistor (Between (5,6) and (7,6))
    ax.plot([5, 7], [6, 6], color="#94a3b8", lw=1.8, zorder=10)
    r150 = patches.Rectangle((5.4, 5.82), 1.2, 0.36, facecolor="#fef3c7", edgecolor="#f59e0b", lw=0.8, zorder=11)
    ax.add_patch(r150)
    ax.text(6, 6.0, "150Ω", color="#92400e", fontsize=6.0, fontweight='bold', ha='center', va='center', zorder=12)

    # 9. ESP32 DevKit Headers (Left: X=3, Y=5..19; Right: X=13, Y=5..19)
    # Female socket headers
    ax.add_patch(patches.Rectangle((2.65, 4.6), 0.7, 14.8, facecolor="#0f172a", edgecolor="#475569", lw=1.2, zorder=8))
    ax.add_patch(patches.Rectangle((12.65, 4.6), 0.7, 14.8, facecolor="#0f172a", edgecolor="#475569", lw=1.2, zorder=8))

    # ESP32 Module Outline (Ghosted preview of plugged-in DevKit)
    esp_box = patches.FancyBboxPatch((2.4, 4.4), 10.4, 15.6, boxstyle="round,pad=0.2,rounding_size=0.4",
                                    facecolor="#0f172a", edgecolor="#38bdf8", lw=1.6, alpha=0.35, zorder=7)
    ax.add_patch(esp_box)
    # ESP32 RF shield
    ax.add_patch(patches.Rectangle((4.5, 5.0), 6.2, 6.5, facecolor="#94a3b8", edgecolor="#cbd5e1", lw=1.0, alpha=0.45, zorder=8))
    ax.text(7.6, 8.2, "ESP-WROOM-32", color="#ffffff", fontsize=8.0, fontweight='bold', ha='center', va='center', zorder=9, alpha=0.85)

    # Micro USB connector at bottom
    ax.add_patch(patches.Rectangle((6.2, 19.3), 2.8, 1.2, facecolor="#94a3b8", edgecolor="#e2e8f0", lw=1.0, zorder=8, alpha=0.7))
    ax.text(7.6, 19.9, "USB", color="#0f172a", fontsize=6.5, fontweight='bold', ha='center', va='center', zorder=9)

    # Left-side ESP32 Pin Labels (offset cleanly outside board)
    esp_pins_left = {
        12: ("GPIO 25\n(FAN PWM)", "#16a34a", -1.8),
        13: ("GPIO 26\n(SERVO)", "#ea580c", -1.8),
        18: ("GND", "#475569", -1.2),
        19: ("VIN (5V)", "#dc2626", -1.2)
    }
    for py, (pname, pcol, xoff) in esp_pins_left.items():
        ax.plot([3], [py], marker='o', markersize=5.0, color=pcol, zorder=15)
        ax.plot([3, xoff + 0.8], [py, py], color=pcol, lw=0.8, linestyle=':', zorder=14)
        ax.text(xoff + 0.6, py, pname, color=pcol, fontsize=6.5, fontweight='bold', ha='right', va='center',
                bbox=dict(boxstyle="round,pad=0.2", fc="#ffffff", ec=pcol, lw=0.8), zorder=16)

    # Right-side ESP32 Pin Labels (offset cleanly outside board)
    esp_pins_right = {
        9:  ("GPIO 21 (CS Meat)", "#db2777", 15.6),
        10: ("GPIO 19 (SO)", "#9333ea", 15.6),
        11: ("GPIO 18 (SCK)", "#7c3aed", 15.6),
        12: ("GPIO 5 (CS Pit)", "#2563eb", 15.6),
        18: ("GND", "#475569", 15.0),
        19: ("3V3 OUT", "#d97706", 15.2)
    }
    for py, (pname, pcol, xoff) in esp_pins_right.items():
        ax.plot([13], [py], marker='o', markersize=5.0, color=pcol, zorder=15)
        ax.plot([13, xoff - 0.2], [py, py], color=pcol, lw=0.8, linestyle=':', zorder=14)
        ax.text(xoff, py, pname, color=pcol, fontsize=6.5, fontweight='bold', ha='left', va='center',
                bbox=dict(boxstyle="round,pad=0.2", fc="#ffffff", ec=pcol, lw=0.8), zorder=16)

    # 10. MAX31855 Thermocouple Breakout Socket Header (5-pin at Row 21, Cols H..L: X=8..12)
    tc_header = patches.Rectangle((7.6, 20.6), 4.8, 0.8, facecolor="#1e293b", edgecolor="#475569", lw=1.2, zorder=10)
    ax.add_patch(tc_header)
    tc_pins = [
        (8,  "3V3", "#d97706"),
        (9,  "GND", "#475569"),
        (10, "SCK", "#7c3aed"),
        (11, "SO",  "#9333ea"),
        (12, "CS1", "#2563eb")
    ]
    for px, plbl, pcol in tc_pins:
        ax.plot([px], [21], marker='s', markersize=4.5, color=pcol, zorder=12)
        ax.text(px, 22.0, plbl, color=pcol, fontsize=6.5, fontweight='bold', ha='center', va='top', zorder=15)
    ax.text(10, 20.2, "MAX31855 THERMOCOUPLE HEADER", color="#cbd5e1", fontsize=6.2, fontweight='bold', ha='center', va='bottom', zorder=12)

def draw_bottom_wiring(ax):
    """Draws bottom-side solder traces, bus wires, and insulated jumpers on the right panel."""
    lw_bus = 4.2      # Heavy bare copper / solder bridge bus
    lw_jumper = 2.4   # Insulated jumper wire

    # --- 1. +5V HIGH CURRENT BUS (RED) ---
    # Solid solder bridge across top edge: (2,1) -> (6,1) -> (12,1)
    ax.plot([2, 6, 12], [1, 1, 1], color="#dc2626", lw=lw_bus, solid_capstyle='round', zorder=10)
    # Drop to Diode Cathode at (6,2)
    ax.plot([6, 6], [1, 2], color="#dc2626", lw=lw_bus, solid_capstyle='round', zorder=10)
    # Drop to Buffer Cap (+) lead at (12,3)
    ax.plot([12, 12], [1, 3], color="#dc2626", lw=lw_bus, solid_capstyle='round', zorder=10)
    # Insulated jumper wire from (2,1) down to ESP32 VIN at (3,19)
    ax.plot([2, 2, 3], [1, 19, 19], color="#ef4444", lw=lw_jumper, linestyle='--', zorder=12)

    # --- 2. COMMON GROUND BUS (BLACK/DARK BLUE) ---
    # Power GND starts at (4,1)
    # Bus drops from (4,1) to (4,3) -> runs across to (9,3) -> drops to MOSFET Source at (9,4) and Pull-Down at (9,5)
    ax.plot([4, 4, 9, 9, 9], [1, 3, 3, 4, 5], color="#1e293b", lw=lw_bus, solid_capstyle='round', zorder=10)
    # Bus to Servo GND at (11,1) and Buffer Cap (-) at (11,3)
    ax.plot([4, 11, 11], [1, 1, 3], color="#1e293b", lw=lw_bus, solid_capstyle='round', zorder=10)
    # Jumper from (4,3) to ESP32 GND at (3,18)
    ax.plot([4, 3], [3, 18], color="#0f172a", lw=lw_jumper, linestyle='--', zorder=12)
    # Ground connection from ESP32 GND (13,18) to MAX31855 GND (9,21)
    ax.plot([13, 9], [18, 21], color="#0f172a", lw=lw_jumper, linestyle='--', zorder=12)

    # --- 3. BLOWER FAN SWITCHED DRAIN RETURN (LIGHT BLUE) ---
    # Solder bridge: Fan (-) at (8,1) -> Diode Anode at (8,2) -> MOSFET Drain at (8,4)
    ax.plot([8, 8, 8], [1, 2, 4], color="#0284c7", lw=lw_bus, solid_capstyle='round', zorder=11)

    # --- 4. BLOWER GATE DRIVE (GREEN) ---
    # ESP32 GPIO 25 at (3,12) -> Jumper to 150 Ohm input at (5,6)
    ax.plot([3, 5], [12, 6], color="#16a34a", lw=lw_jumper, linestyle='--', zorder=12)
    # Solder bridge: 150 Ohm output at (7,6) -> MOSFET Gate at (7,4) & 100k Pull-Down at (7,5)
    ax.plot([7, 7, 7], [6, 5, 4], color="#15803d", lw=lw_bus, solid_capstyle='round', zorder=11)

    # --- 5. SERVO DAMPER PWM SIGNAL (ORANGE) ---
    # ESP32 GPIO 26 at (3,13) -> Jumper to Servo Header Signal pin at (13,1)
    ax.plot([3, 1.2, 1.2, 13], [13, 13, 0.4, 1], color="#ea580c", lw=lw_jumper, linestyle='--', zorder=12)

    # --- 6. 3.3V SENSOR LOGIC POWER (AMBER) ---
    # ESP32 3V3 at (13,19) -> Jumper to MAX31855 VCC at (8,21)
    ax.plot([13, 13.4, 13.4, 8], [19, 19, 21.6, 21], color="#f59e0b", lw=lw_jumper, linestyle='--', zorder=12)

    # --- 7. SPI BUS TO MAX31855 (PURPLE & BLUE) ---
    # SCK: GPIO 18 at (13,11) -> (10,21)
    ax.plot([13, 10], [11, 21], color="#8b5cf6", lw=lw_jumper, linestyle='--', zorder=12)
    # SO/MISO: GPIO 19 at (13,10) -> (11,21)
    ax.plot([13, 11], [10, 21], color="#a855f7", lw=lw_jumper, linestyle='--', zorder=12)
    # CS (Pit): GPIO 5 at (13,12) -> (12,21)
    ax.plot([13, 12], [12, 21], color="#3b82f6", lw=lw_jumper, linestyle='--', zorder=12)

    # Solder connection joint dots
    solder_joints = [
        (2,1), (4,1), (6,1), (8,1), (11,1), (12,1), (13,1), # Top row headers
        (6,2), (8,2),                                       # Diode
        (11,3), (12,3),                                     # Capacitor
        (7,4), (8,4), (9,4),                                # MOSFET Gate, Drain, Source
        (7,5), (9,5),                                       # 100k Resistor
        (5,6), (7,6),                                       # 150R Resistor
        (3,12), (3,13), (3,18), (3,19),                     # ESP32 Left Pins
        (13,10), (13,11), (13,12), (13,18), (13,19),        # ESP32 Right Pins
        (8,21), (9,21), (10,21), (11,21), (12,21)          # MAX31855 Header
    ]
    for jx, jy in solder_joints:
        ax.plot([jx], [jy], marker='o', markersize=6.0, color="#fef08a", markeredgecolor="#854d0e", markeredgewidth=1.2, zorder=20)

    # Right-side Wiring Legend Box placed cleanly in right margin
    legend_items = [
        ("Solid Red", "#dc2626", lw_bus, "-", "+5V High-Current Bus"),
        ("Solid Dark Slate", "#1e293b", lw_bus, "-", "GND Common Return Bus"),
        ("Solid Blue", "#0284c7", lw_bus, "-", "Switched Fan Drain Bridge"),
        ("Dashed Red", "#ef4444", lw_jumper, "--", "+5V to ESP32 VIN"),
        ("Dashed Dark Slate", "#0f172a", lw_jumper, "--", "GND to ESP32 / Sensors"),
        ("Dashed Green", "#16a34a", lw_jumper, "--", "GPIO 25 PWM to Gate Resistor"),
        ("Solid Forest Green", "#15803d", lw_bus, "-", "Gate Resistor to MOSFET Gate"),
        ("Dashed Orange", "#ea580c", lw_jumper, "--", "GPIO 26 PWM to Servo"),
        ("Dashed Amber", "#f59e0b", lw_jumper, "--", "3.3V Logic to MAX31855"),
        ("Dashed Purple", "#8b5cf6", lw_jumper, "--", "SPI SCK (GPIO 18)"),
        ("Dashed Violet", "#a855f7", lw_jumper, "--", "SPI MISO (GPIO 19)"),
        ("Dashed Blue", "#3b82f6", lw_jumper, "--", "SPI CS Pit (GPIO 5)")
    ]
    
    # Legend panel outside board on right
    lx = 15.2
    ly_start = 4.5
    ax.text(lx, ly_start - 0.8, "WIRING COLOR CODE", fontsize=8.0, fontweight='bold', color='#0f172a')
    for idx, (label, col, lw, ls, desc) in enumerate(legend_items):
        y_pos = ly_start + idx * 1.3
        ax.plot([lx, lx + 1.2], [y_pos, y_pos], color=col, lw=lw, linestyle=ls)
        ax.text(lx + 1.5, y_pos, desc, fontsize=6.8, va='center', color='#1e293b', fontweight='medium')

def main():
    fig, (ax_top, ax_bot) = plt.subplots(1, 2, figsize=(24, 15), dpi=200)
    fig.patch.set_facecolor('#ffffff')

    # Draw Top and Bottom Views
    draw_perfboard_base(
        ax_top, 
        "TOP VIEW — Component Footprints & Socket Headers",
        "Viewed from above: Plug in ESP32, components, terminals, and pin headers"
    )
    draw_top_components(ax_top)

    draw_perfboard_base(
        ax_bot, 
        "BOTTOM VIEW (X-Ray) — Solder Bridges & Insulated Wire Runs",
        "Viewed looking through board from top: Pad coordinates A1..N22 match 1:1"
    )
    draw_bottom_wiring(ax_bot)

    # Master Title Header
    fig.text(0.5, 0.972, "ESP32 Smoker Controller — 40x60mm Green Perfboard Circuit Layout",
             ha='center', va='top', fontsize=20, fontweight='bold', color='#0f172a')
    fig.text(0.5, 0.945, "Standard 2.54mm (0.1\") Double-Sided Green Prototyping Board (14 Columns x 22 Rows) — Sized for 3D Chassis Bay",
             ha='center', va='top', fontsize=11.5, color='#475569', style='italic')

    # Hardware & Bill of Materials Banner at Bottom
    bom_text = (
        "BILL OF MATERIALS & PACKAGING:   "
        "• Board: 4x6 cm (40x60mm) Double-Sided Green FR4 Perfboard (14x22 plated holes)   |   "
        "• ESP32 Socket: 2x 15-Pin 2.54mm Female Headers   |   "
        "• MOSFET: 1x IRLZ44N (TO-220 N-Channel Logic-Level)\n"
        "• Flyback Diode: 1x 1N5819 Schottky   |   "
        "• Resistors: 1x 150Ω 1/4W (Gate) & 1x 100kΩ 1/4W (Pull-Down)   |   "
        "• Capacitor: 1x 1000µF 16V Radial Electrolytic\n"
        "• Connectors: 2x 2-Pin 5.08mm Screw Terminals (5V Power, Blower Fan), 1x 3-Pin 2.54mm Male Header (Servo), 1x 5-Pin Female Header (MAX31855)\n"
        "SOLDERING SPECIFICATION:   Solid Heavy Lines = Bare Copper Solder Bridges / Bus Wires on Bottom   |   Dashed Lines = 28-30 AWG Insulated Hookup / Wire-Wrap Jumpers on Bottom"
    )
    fig.text(0.5, 0.042, bom_text, ha='center', va='center', fontsize=9.2,
             bbox=dict(boxstyle="square,pad=0.6", fc='#f1f5f9', ec='#94a3b8', lw=1.2),
             color='#0f172a')

    plt.subplots_adjust(left=0.03, right=0.97, top=0.91, bottom=0.08, wspace=0.10)

    out_png = os.path.join(DOCS_IMG_DIR, "perfboard_circuit_layout.png")
    out_jpg = os.path.join(DOCS_IMG_DIR, "perfboard_circuit_layout.jpg")
    out_cad_jpg = os.path.join(REPO_ROOT, "cad", "perfboard_circuit_layout.jpg")

    plt.savefig(out_png, dpi=200, facecolor='#ffffff')
    print(f"Saved {out_png}")

    im = Image.open(out_png).convert('RGB')
    im.save(out_jpg, 'JPEG', quality=95)
    im.save(out_cad_jpg, 'JPEG', quality=95)
    print(f"Saved {out_jpg} and {out_cad_jpg}")

if __name__ == "__main__":
    main()
