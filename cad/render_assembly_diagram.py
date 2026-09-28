#!/usr/bin/env python3
"""
ESP32 Smoker Controller — 3D Printed Parts Assembly Diagram Generator
Renders an exploded technical CAD diagram directly from the exact STL meshes in cad/stl/.
"""

import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from mpl_toolkits.mplot3d import proj3d
from stl import mesh
from PIL import Image

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STL_DIR = os.path.join(REPO_ROOT, "cad", "stl")

def load_stl(filename):
    path = os.path.join(STL_DIR, filename)
    m = mesh.Mesh.from_file(path)
    return m.vectors.copy()

def rotate_x(v, deg):
    rad = np.radians(deg)
    c, s = np.cos(rad), np.sin(rad)
    R = np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    return np.einsum('ijk,lk->ijl', v, R)

def rotate_y(v, deg):
    rad = np.radians(deg)
    c, s = np.cos(rad), np.sin(rad)
    R = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.einsum('ijk,lk->ijl', v, R)

def rotate_z(v, deg):
    rad = np.radians(deg)
    c, s = np.cos(rad), np.sin(rad)
    R = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return np.einsum('ijk,lk->ijl', v, R)

def compute_normals(v):
    v0, v1, v2 = v[:, 0, :], v[:, 1, :], v[:, 2, :]
    normals = np.cross(v1 - v0, v2 - v0)
    norms = np.linalg.norm(normals, axis=1, keepdims=True)
    norms[norms == 0] = 1.0
    return normals / norms

def compute_shading(v, base_rgb):
    normals = compute_normals(v)
    # Primary key light
    light1 = np.array([0.45, -0.55, 0.70])
    light1 = light1 / np.linalg.norm(light1)
    # Secondary fill light
    light2 = np.array([-0.50, -0.40, 0.30])
    light2 = light2 / np.linalg.norm(light2)
    # Top rim light
    light3 = np.array([0.0, 0.2, 0.9])
    light3 = light3 / np.linalg.norm(light3)

    dot1 = np.clip(np.dot(normals, light1), 0, 1)
    dot2 = np.clip(np.dot(normals, light2), 0, 1)
    dot3 = np.clip(np.dot(normals, light3), 0, 1)

    ambient = 0.35
    intensity = ambient + 0.44 * dot1 + 0.18 * dot2 + 0.12 * dot3
    intensity = np.clip(intensity, 0.22, 1.0)

    colors = np.zeros((len(normals), 4))
    colors[:, 0] = base_rgb[0] * intensity
    colors[:, 1] = base_rgb[1] * intensity
    colors[:, 2] = base_rgb[2] * intensity
    colors[:, 3] = base_rgb[3] if len(base_rgb) > 3 else 1.0
    return colors

def main():
    # 1. Load exact STL parts
    v_housing = load_stl("smoker_housing.stl")
    v_rotor = load_stl("damper_rotor.stl")
    v_nozzle = load_stl("bbq_guru_vision_pro_nozzle_adapter.stl")
    v_fancov = load_stl("fan_cover.stl")
    v_lid = load_stl("electronics_lid.stl")
    v_plate = load_stl("vision_pro_kamado_slide_plate.stl")

    # 2. Transformations for Exploded View
    # Damper Rotor (exploded along -X out of housing bore)
    v_rotor_exp = rotate_y(v_rotor, 90)
    v_rotor_exp = rotate_x(v_rotor_exp, 45) # Expose aperture
    v_rotor_exp += np.array([-82.0, 91.4, 8.7])

    # Output Nozzle Adapter (exploded along +Y)
    v_nozzle_exp = rotate_x(v_nozzle, -90)
    nozzle_min = v_nozzle_exp.min(axis=(0, 1))
    nozzle_max = v_nozzle_exp.max(axis=(0, 1))
    nozzle_mid = (nozzle_min + nozzle_max) / 2.0
    v_nozzle_exp += np.array([27.4 - nozzle_mid[0], 195.0 - nozzle_min[1], 8.7 - nozzle_mid[2]])

    # Fan Cover (exploded along +Z above fan pocket)
    v_fancov_exp = v_fancov.copy()
    v_fancov_exp += np.array([0.0, 0.0, 78.0])

    # Electronics Bay Lid (exploded along +Z above electronics bay)
    v_lid_exp = v_lid.copy()
    v_lid_exp += np.array([-4.0, -70.4, 78.0])

    # Vision Pro Slide Plate (exploded along +Y in front of nozzle)
    v_plate_exp = rotate_z(v_plate, -90)
    plate_min = v_plate_exp.min(axis=(0, 1))
    plate_max = v_plate_exp.max(axis=(0, 1))
    v_plate_exp += np.array([27.4 - 0.0, 288.0 - plate_min[1], 8.7 - 0.0])

    # 3. Canvas setup
    fig = plt.figure(figsize=(24, 15), dpi=200)
    ax = fig.add_axes([0.10, 0.06, 0.85, 0.84], projection='3d')
    ax.set_facecolor('#ffffff')
    fig.patch.set_facecolor('#ffffff')

    # Color palette (technical engineering)
    c_housing = [0.20, 0.26, 0.35, 1.0]   # Slate Gray
    c_rotor   = [0.95, 0.42, 0.08, 1.0]   # Safety Orange
    c_nozzle  = [0.06, 0.52, 0.86, 1.0]   # Sky Blue
    c_fancov  = [0.06, 0.65, 0.48, 1.0]   # Emerald Green
    c_lid     = [0.88, 0.58, 0.10, 1.0]   # Warm Amber
    c_plate   = [0.85, 0.16, 0.28, 1.0]   # Ruby Red

    parts = [
        (v_housing, c_housing, "smoker_housing.stl"),
        (v_rotor_exp, c_rotor, "damper_rotor.stl"),
        (v_nozzle_exp, c_nozzle, "bbq_guru_vision_pro_nozzle_adapter.stl"),
        (v_fancov_exp, c_fancov, "fan_cover.stl"),
        (v_lid_exp, c_lid, "electronics_lid.stl"),
        (v_plate_exp, c_plate, "vision_pro_kamado_slide_plate.stl"),
    ]

    for verts, color, name in parts:
        shading = compute_shading(verts, color)
        poly = Poly3DCollection(verts, facecolors=shading, edgecolors=[0.12, 0.12, 0.12, 0.08], linewidths=0.18)
        ax.add_collection3d(poly)

    # Assembly alignment guidelines
    ax.plot([-92, 60], [91.4, 91.4], [8.7, 8.7], 'k--', lw=1.3, alpha=0.40)
    ax.plot([27.4, 27.4], [90, 312], [8.7, 8.7], 'k--', lw=1.3, alpha=0.40)
    ax.plot([27.4, 27.4], [27.4, 27.4], [10, 88], 'k--', lw=1.3, alpha=0.40)
    ax.plot([27.4, 27.4], [-35.2, -35.2], [15, 88], 'k--', lw=1.3, alpha=0.40)

    ax.view_init(elev=26, azim=-53)

    all_v = np.concatenate([v_housing, v_rotor_exp, v_nozzle_exp, v_fancov_exp, v_lid_exp, v_plate_exp], axis=0)
    pts = all_v.reshape(-1, 3)
    min_xyz = pts.min(axis=0)
    max_xyz = pts.max(axis=0)

    span_x = (max_xyz[0] - min_xyz[0])
    span_y = (max_xyz[1] - min_xyz[1])
    span_z = (max_xyz[2] - min_xyz[2])

    ax.set_xlim(min_xyz[0] - 12, max_xyz[0] + 12)
    ax.set_ylim(min_xyz[1] - 12, max_xyz[1] + 12)
    ax.set_zlim(min_xyz[2] - 12, max_xyz[2] + 12)

    ax.set_box_aspect((span_x, span_y, span_z), zoom=1.65)
    ax.set_axis_off()

    fig.canvas.draw()
    proj_mat = ax.get_proj()

    def get_2d(pt3d):
        x2, y2, _ = proj3d.proj_transform(pt3d[0], pt3d[1], pt3d[2], proj_mat)
        disp = ax.transData.transform((x2, y2))
        return fig.transFigure.inverted().transform(disp)

    # Precise 3D anchor points on actual parts
    anchors = {
        "rotor": np.array([-72.0, 91.4, 20.0]),
        "housing": np.array([-4.0, -35.0, 10.0]),
        "nozzle": np.array([38.0, 195.0, 18.0]),
        "plate": np.array([27.4, 290.0, 26.0]),
        "fancov": np.array([27.4, 27.4, 82.0]),
        "lid": np.array([15.0, -35.0, 82.0]),
    }

    # 4. Title Header
    fig.text(0.5, 0.968, "ESP32 Smoker Controller — 3D Printed Parts Assembly Diagram",
             ha='center', va='top', fontsize=22, fontweight='bold', color='#0f172a')
    fig.text(0.5, 0.940, "Exploded Technical CAD View — Exact Geometry Rendered Directly From cad/stl/*.stl",
             ha='center', va='top', fontsize=12.5, color='#475569', style='italic')

    # Callouts configuration
    callout_defs = [
        {
            "key": "plate",
            "title": "Part 6: vision_pro_kamado_slide_plate.stl",
            "desc": (
                "• Vision Kamado Pro S-Series draft door slide plate\n"
                "• 195mm curvature radius, drops directly into grill track\n"
                "• Standard 31.8mm (1-1/4\") female blower receiver port\n"
                "• Includes top pull handle & keeper hole for lanyard plug"
            ),
            "box_xy": (0.73, 0.88),
            "arrow_start": (0.75, 0.74),
            "bg_color": '#fef2f2',
            "border_col": '#dc2626',
            "rad": -0.08
        },
        {
            "key": "nozzle",
            "title": "Part 3: bbq_guru_vision_pro_nozzle_adapter.stl",
            "desc": (
                "• BBQ Guru Vision Pro / Pit Viper compatible nozzle adapter\n"
                "• 31.5mm OD nozzle with 2.8mm silicone O-ring retention groove\n"
                "• Mechanical stop collar seats flush against slide plate\n"
                "• 4x M3 countersunk mounting ears bolt to main chassis"
            ),
            "box_xy": (0.73, 0.42),
            "arrow_start": (0.73, 0.38),
            "bg_color": '#f0f9ff',
            "border_col": '#0284c7',
            "rad": -0.10
        },
        {
            "key": "fancov",
            "title": "Part 4: fan_cover.stl",
            "desc": (
                "• 5015 centrifugal blower intake protective grille\n"
                "• 54.8 x 54.8 x 2.0 mm with crossbar safety mesh\n"
                "• 3x M3 corner fastener holes secure fan and cover"
            ),
            "box_xy": (0.38, 0.78),
            "arrow_start": (0.45, 0.65),
            "bg_color": '#f0fdf4',
            "border_col": '#16a34a',
            "rad": 0.04
        },
        {
            "key": "rotor",
            "title": "Part 2: damper_rotor.stl",
            "desc": (
                "• Rotating barrel damper valve (28mm OD x 32mm L)\n"
                "• Precision dual metering apertures for linear airflow control\n"
                "• Internal spline socket for SG90 / MG90S micro-servo horn\n"
                "• Inserts laterally into housing cylindrical bore"
            ),
            "box_xy": (0.04, 0.82),
            "arrow_start": (0.23, 0.70),
            "bg_color": '#fff7ed',
            "border_col": '#ea580c',
            "rad": -0.05
        },
        {
            "key": "lid",
            "title": "Part 5: electronics_lid.stl",
            "desc": (
                "• Electronics bay protective lid (62.8 x 70.4 x 4.0 mm)\n"
                "• Beveled perimeter lip with wire pass-through slot\n"
                "• Dual snap-latch ribs + 2x M3 retention screw anchors\n"
                "• Protects ESP32, MAX31855, and MOSFET circuitry"
            ),
            "box_xy": (0.04, 0.54),
            "arrow_start": (0.23, 0.47),
            "bg_color": '#fffbeb',
            "border_col": '#d97706',
            "rad": 0.04
        },
        {
            "key": "housing",
            "title": "Part 1: smoker_housing.stl",
            "desc": (
                "• Main unibody chassis (162 x 58.8 x 26.8 mm)\n"
                "• Integrates 5015 blower chamber, rotary damper sleeve & servo mount\n"
                "• Rear compartment houses ESP32, thermocouple amplifier & power circuit\n"
                "• Rear RJ45 / USB umbilical pass-through with reinforced strain relief"
            ),
            "box_xy": (0.04, 0.26),
            "arrow_start": (0.23, 0.22),
            "bg_color": '#f8fafc',
            "border_col": '#334155',
            "rad": -0.04
        },
    ]

    # Create overlay 2D axis for arrows and callouts
    ax_ann = fig.add_axes([0, 0, 1, 1], frame_on=False)
    ax_ann.set_xlim(0, 1)
    ax_ann.set_ylim(0, 1)
    ax_ann.set_axis_off()

    for c in callout_defs:
        pt3d = anchors[c["key"]]
        xy_target = get_2d(pt3d)

        full_text = f"{c['title']}\n{c['desc']}"
        bx, by = c["box_xy"]

        # Leader arrow
        ax_ann.annotate(
            "",
            xy=(xy_target[0], xy_target[1]),
            xytext=c["arrow_start"],
            arrowprops=dict(
                arrowstyle="-|>",
                color=c["border_col"],
                lw=1.8,
                shrinkA=2,
                shrinkB=4,
                mutation_scale=15,
                connectionstyle=f"arc3,rad={c['rad']}"
            ),
            zorder=5
        )

        # Callout text box
        ax_ann.text(bx, by, full_text,
                    fontsize=9.8, family='sans-serif',
                    bbox=dict(boxstyle="round,pad=0.7,rounding_size=0.4",
                              fc=c["bg_color"], ec=c["border_col"], lw=1.8, alpha=0.97),
                    va='top', ha='left', zorder=10)

    # 5. Bottom Hardware Specification Bar
    legend_text = (
        "ASSEMBLY & FASTENER SPECIFICATION:   "
        "• 4x M3 x 12mm Socket Head Cap Screws: Secures Nozzle Adapter (Part 3) to Housing (Part 1)   |   "
        "• 3x M3 x 16mm Cap Screws: Secures Fan Cover (Part 4) & 5015 Blower to Housing (Part 1)\n"
        "• 1x MG90S / SG90 Micro Servo + M2 Horn Screw: Secures Damper Rotor (Part 2)   |   "
        "• 1x Dash-121 Silicone O-Ring (1-1/16\" ID x 1-1/4\" OD): Friction seal on Nozzle (Part 3)   |   "
        "• 2x M3 x 8mm Screws: Electronics Lid (Part 5)"
    )
    ax_ann.text(0.5, 0.032, legend_text, ha='center', va='center', fontsize=10.0,
                bbox=dict(boxstyle="square,pad=0.6", fc='#f1f5f9', ec='#94a3b8', lw=1.2),
                color='#0f172a', zorder=10)

    # 6. Save outputs
    out_png = os.path.join(REPO_ROOT, "docs", "images", "parts_assembly_diagram.png")
    out_jpg = os.path.join(REPO_ROOT, "docs", "images", "parts_assembly_diagram.jpg")
    out_cad_jpg = os.path.join(REPO_ROOT, "cad", "parts_assembly_diagram.jpg")

    plt.savefig(out_png, dpi=200, facecolor='#ffffff')
    print(f"Saved {out_png}")

    im = Image.open(out_png).convert('RGB')
    im.save(out_jpg, 'JPEG', quality=95)
    im.save(out_cad_jpg, 'JPEG', quality=95)
    print(f"Saved JPEG versions to {out_jpg} and {out_cad_jpg}")

if __name__ == "__main__":
    main()
