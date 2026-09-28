#!/usr/bin/env python3
"""
Roto-Damper (RD3 Style) Damper System — 3D CAD Exploded Assembly Diagram Generator
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
    light1 = np.array([0.45, -0.55, 0.70])
    light1 /= np.linalg.norm(light1)
    light2 = np.array([-0.50, -0.40, 0.30])
    light2 /= np.linalg.norm(light2)
    light3 = np.array([0.0, 0.2, 0.9])
    light3 /= np.linalg.norm(light3)

    dot1 = np.clip(np.dot(normals, light1), 0, 1)
    dot2 = np.clip(np.dot(normals, light2), 0, 1)
    dot3 = np.clip(np.dot(normals, light3), 0, 1)

    ambient = 0.38
    intensity = ambient + 0.42 * dot1 + 0.16 * dot2 + 0.12 * dot3
    intensity = np.clip(intensity, 0.22, 1.0)

    colors = np.zeros((len(normals), 4))
    colors[:, 0] = base_rgb[0] * intensity
    colors[:, 1] = base_rgb[1] * intensity
    colors[:, 2] = base_rgb[2] * intensity
    colors[:, 3] = base_rgb[3] if len(base_rgb) > 3 else 1.0
    return colors

def make_box(min_xyz, max_xyz):
    x0, y0, z0 = min_xyz
    x1, y1, z1 = max_xyz
    verts = [
        # bottom
        [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0]],
        [[x0, y0, z0], [x1, y1, z0], [x0, y1, z0]],
        # top
        [[x0, y0, z1], [x1, y1, z1], [x1, y0, z1]],
        [[x0, y0, z1], [x0, y1, z1], [x1, y1, z1]],
        # front
        [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1]],
        [[x0, y0, z0], [x1, y0, z1], [x0, y0, z1]],
        # back
        [[x0, y1, z0], [x1, y1, z1], [x1, y1, z0]],
        [[x0, y1, z0], [x0, y1, z1], [x1, y1, z1]],
        # left
        [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1]],
        [[x0, y0, z0], [x0, y1, z1], [x0, y1, z0]],
        # right
        [[x1, y0, z0], [x1, y1, z1], [x1, y0, z1]],
        [[x1, y0, z0], [x1, y1, z0], [x1, y1, z1]],
    ]
    return np.array(verts, dtype=float)

def make_cylinder(r, h, center_xy, z_base, n_segs=24):
    cx, cy = center_xy
    theta = np.linspace(0, 2*np.pi, n_segs, endpoint=False)
    x = cx + r * np.cos(theta)
    y = cy + r * np.sin(theta)
    verts = []
    # sides
    for i in range(n_segs):
        j = (i + 1) % n_segs
        p1 = [x[i], y[i], z_base]
        p2 = [x[j], y[j], z_base]
        p3 = [x[j], y[j], z_base + h]
        p4 = [x[i], y[i], z_base + h]
        verts.append([p1, p2, p3])
        verts.append([p1, p3, p4])
    # top cap
    for i in range(1, n_segs - 1):
        verts.append([[x[0], y[0], z_base + h], [x[i], y[i], z_base + h], [x[i+1], y[i+1], z_base + h]])
    # bottom cap
    for i in range(1, n_segs - 1):
        verts.append([[x[0], y[0], z_base], [x[i+1], y[i+1], z_base], [x[i], y[i], z_base]])
    return np.array(verts, dtype=float)

def make_torus(r_maj, r_min, center_xyz, axis='y', n_maj=32, n_min=16):
    u = np.linspace(0, 2*np.pi, n_maj, endpoint=False)
    v = np.linspace(0, 2*np.pi, n_min, endpoint=False)
    u_grid, v_grid = np.meshgrid(u, v)
    x = (r_maj + r_min * np.cos(v_grid)) * np.cos(u_grid)
    z = (r_maj + r_min * np.cos(v_grid)) * np.sin(u_grid)
    y = r_min * np.sin(v_grid)
    verts = []
    for i in range(n_min):
        i_next = (i + 1) % n_min
        for j in range(n_maj):
            j_next = (j + 1) % n_maj
            p1 = [x[i, j], y[i, j], z[i, j]]
            p2 = [x[i, j_next], y[i, j_next], z[i, j_next]]
            p3 = [x[i_next, j_next], y[i_next, j_next], z[i_next, j_next]]
            p4 = [x[i_next, j], y[i_next, j], z[i_next, j]]
            verts.append([p1, p2, p3])
            verts.append([p1, p3, p4])
    verts = np.array(verts, dtype=float)
    if axis == 'y':
        # Torus around Y axis (in XZ plane)
        pass
    verts += np.array(center_xyz)
    return verts

def main():
    print("Loading 3D STL meshes...")
    v_base = load_stl("rotodamper_base.stl")
    v_disc = load_stl("rotodamper_disc.stl")
    v_cap  = load_stl("rotodamper_cap.stl")
    v_fancov = load_stl("rotodamper_fan_cover.stl")
    v_plate = load_stl("vision_pro_kamado_slide_plate.stl")

    # 1. Base Mesh (lower shell at origin, Z: 0 to 20.4)
    v_base_exp = v_base.copy()

    # 2. Disc Mesh (exploded upward along +Z between base and cap)
    # Rotate disc by 35 degrees to clearly reveal the 80-degree sector aperture
    v_disc_exp = rotate_z(v_disc, 35)
    v_disc_exp += np.array([0.0, 0.0, 46.0])

    # 3. Cap Mesh (exploded upward along +Z)
    v_cap_exp = v_cap.copy()
    v_cap_exp += np.array([0.0, 0.0, 92.0])

    # 4. Fan Cover Mesh (exploded downward along -Z below base & fan)
    v_fancov_exp = v_fancov.copy()
    v_fancov_exp += np.array([0.0, 0.0, -56.0])

    # 5. Slide Plate Mesh (exploded along +Y in front of BBQ Guru nozzle)
    v_plate_rot = rotate_z(v_plate, -90)
    # Cap nozzle center is at (X=0, Y=58, Z=12 + 92 = 104)
    v_plate_exp = v_plate_rot + np.array([0.0, 168.0 - (-205.16), 104.0])

    # 6. Hardware Mocks
    # 5015 Centrifugal Blower Fan (between base and fan cover)
    v_fan = make_box([-25.0, -73.0, -32.0], [25.0, -23.0, -17.0])

    # MG90S Micro Servo (exploded above cap)
    v_servo_body = make_box([-6.2, -23.0 + 5.8, 142.0], [6.2, 5.8, 166.0])
    v_servo_flange = make_box([-6.2, -23.0 + 5.8 - 4.5, 155.0], [6.2, 5.8 + 4.5, 157.5])
    v_servo_spline = make_cylinder(2.4, 6.0, (0.0, 0.0), 136.0, n_segs=20)
    v_servo = np.concatenate([v_servo_body, v_servo_flange], axis=0)

    # Dash-121 O-Ring (floating between nozzle tip and slide plate receiver)
    v_oring = make_torus(14.3, 1.2, [0.0, 112.0, 104.0], axis='y', n_maj=32, n_min=16)

    # Fasteners
    # 4x M3 Cap Screws (above cap)
    screws_m3_cap = []
    for sx, sy in [(-22, 16), (22, 16), (-22, -66), (22, -66)]:
        sc = make_cylinder(2.8, 4.0, (sx, sy), 126.0, n_segs=14)
        sh = make_cylinder(1.5, 14.0, (sx, sy), 112.0, n_segs=14)
        screws_m3_cap.append(np.concatenate([sc, sh], axis=0))
    v_screws_top = np.concatenate(screws_m3_cap, axis=0)

    # Disc Center Pivot Screw
    v_screw_disc = make_cylinder(2.6, 2.5, (0.0, 0.0), 57.0, n_segs=14)
    v_screw_disc_sh = make_cylinder(1.4, 8.0, (0.0, 0.0), 49.0, n_segs=14)
    v_disc_screw = np.concatenate([v_screw_disc, v_screw_disc_sh], axis=0)

    # 4x M3 Fan Cover Screws (below fan cover)
    screws_m3_fan = []
    for sx, sy in [(-20, -76 + 2.4 + 5), (20, -76 + 2.4 + 5), (-20, -76 + 2.4 + 45), (20, -76 + 2.4 + 45)]:
        sc = make_cylinder(2.8, 2.5, (sx, sy), -70.0, n_segs=14)
        sh = make_cylinder(1.5, 10.0, (sx, sy), -67.5, n_segs=14)
        screws_m3_fan.append(np.concatenate([sc, sh], axis=0))
    v_screws_bot = np.concatenate(screws_m3_fan, axis=0)

    # 7. Canvas Setup
    print("Setting up 3D render scene...")
    fig = plt.figure(figsize=(24, 15), dpi=200)
    ax = fig.add_axes([0.08, 0.12, 0.84, 0.80], projection='3d')
    ax.set_facecolor('#ffffff')
    fig.patch.set_facecolor('#ffffff')

    # Color Palette
    c_base     = [0.22, 0.28, 0.38, 1.0]  # Technical Slate Gray/Navy
    c_disc     = [0.95, 0.44, 0.08, 1.0]  # Safety Orange (Sector Disc)
    c_cap      = [0.10, 0.50, 0.82, 1.0]  # Industrial Blue (Upper Housing)
    c_fancov   = [0.12, 0.58, 0.42, 1.0]  # Emerald Teal (Fan Cover)
    c_plate    = [0.85, 0.18, 0.28, 1.0]  # Ruby Red (Vision Pro Slide Plate)
    c_fan      = [0.18, 0.18, 0.20, 0.95] # Carbon Black (5015 Blower Fan)
    c_servo    = [0.15, 0.28, 0.72, 0.95] # Servo Blue (MG90S)
    c_spline   = [0.88, 0.78, 0.22, 1.0]  # Brass Gear
    c_oring    = [0.92, 0.20, 0.12, 1.0]  # Silicone Red/Orange
    c_hardware = [0.72, 0.74, 0.78, 1.0]  # Stainless Steel Silver

    render_parts = [
        (v_base_exp, c_base),
        (v_disc_exp, c_disc),
        (v_cap_exp, c_cap),
        (v_fancov_exp, c_fancov),
        (v_plate_exp, c_plate),
        (v_fan, c_fan),
        (v_servo, c_servo),
        (v_servo_spline, c_spline),
        (v_oring, c_oring),
        (v_screws_top, c_hardware),
        (v_disc_screw, c_hardware),
        (v_screws_bot, c_hardware),
    ]

    for verts, col in render_parts:
        shading = compute_shading(verts, col)
        poly = Poly3DCollection(verts, facecolors=shading, edgecolors=[0.12, 0.12, 0.12, 0.08], linewidths=0.18)
        ax.add_collection3d(poly)

    # 8. Technical Alignment Guidelines
    # Main Coaxial Drive Axis (MG90S -> Disc Hub -> Base Pivot)
    ax.plot([0, 0], [0, 0], [18, 170], 'k--', lw=1.6, alpha=0.55, label='Coaxial Drive Axis')
    # Airflow Nozzle -> O-Ring -> Slide Plate Receiver
    ax.plot([0, 0], [28, 195], [104, 104], 'b--', lw=1.5, alpha=0.50, label='Airflow Centerline')
    # Blower Fan -> Base Pocket -> Fan Cover Stack
    ax.plot([0, 0], [-48, -48], [-62, 10], 'g--', lw=1.4, alpha=0.45, label='Fan Axis')
    # 4x Cap Fastener Line
    for sx, sy in [(-22, 16), (22, 16), (-22, -66), (22, -66)]:
        ax.plot([sx, sx], [sy, sy], [-5, 130], 'k:', lw=1.0, alpha=0.35)

    # 9. Camera View Angle
    ax.view_init(elev=24, azim=-50)

    # Calculate bounding box for auto-scaling
    all_v = np.concatenate([v_base_exp, v_disc_exp, v_cap_exp, v_fancov_exp, v_plate_exp, v_servo], axis=0)
    pts = all_v.reshape(-1, 3)
    min_xyz = pts.min(axis=0)
    max_xyz = pts.max(axis=0)

    span_x = (max_xyz[0] - min_xyz[0])
    span_y = (max_xyz[1] - min_xyz[1])
    span_z = (max_xyz[2] - min_xyz[2])

    ax.set_xlim(min_xyz[0] - 15, max_xyz[0] + 15)
    ax.set_ylim(min_xyz[1] - 15, max_xyz[1] + 15)
    ax.set_zlim(min_xyz[2] - 15, max_xyz[2] + 15)
    ax.set_box_aspect((span_x, span_y, span_z), zoom=1.50)
    ax.set_axis_off()

    fig.canvas.draw()
    proj_mat = ax.get_proj()

    def get_2d(pt3d):
        x2, y2, _ = proj3d.proj_transform(pt3d[0], pt3d[1], pt3d[2], proj_mat)
        disp = ax.transData.transform((x2, y2))
        return fig.transFigure.inverted().transform(disp)

    # Precise 3D anchor points
    anchors = {
        "servo":   np.array([-6.0, 0.0, 155.0]),
        "cap":     np.array([28.0, -10.0, 102.0]),
        "nozzle":  np.array([0.0, 85.0, 104.0]),
        "oring":   np.array([0.0, 112.0, 104.0]),
        "plate":   np.array([25.0, 168.0, 104.0]),
        "disc":    np.array([-20.0, 10.0, 48.0]),
        "base":    np.array([28.0, -35.0, 12.0]),
        "fan":     np.array([-18.0, -45.0, -25.0]),
        "fancov":  np.array([22.0, -45.0, -78.0]),
    }

    # 10. Title Header
    fig.text(0.5, 0.968, "Roto-Damper (RD3 Style) Damper System — 3D CAD Exploded Assembly Diagram",
             ha='center', va='top', fontsize=22, fontweight='bold', color='#0f172a')
    fig.text(0.5, 0.940, "Exact STL Geometry Rendered From cad/stl/*.stl — Coaxial Rotary Sector Valve with 5015 Blower",
             ha='center', va='top', fontsize=12.5, color='#475569', style='italic')

    # 11. Engineering Callout Annotations
    callouts = [
        {
            "key": "servo",
            "title": "MG90S Metal Gear Micro Servo",
            "desc": (
                "• TowerPro MG90S 9g metal-gear micro servo (4.8V-6.0V)\n"
                "• Inverted direct coaxial drive down into disc hub\n"
                "• Zero geartrain lash or linkage play for linear control\n"
                "• 2x M2 mounting screws secure flange into cap bay"
            ),
            "box_xy": (0.04, 0.90),
            "arrow_start": (0.24, 0.81),
            "bg_color": '#eff6ff',
            "border_col": '#2563eb',
            "rad": -0.02
        },
        {
            "key": "disc",
            "title": "Part 2: rotodamper_disc.stl",
            "desc": (
                "• 48.0mm OD x 2.5mm thick rotary sector disc\n"
                "• 80° sector aperture: 288.5 mm² open area (= 5015 fan throat)\n"
                "• 10° solid safety overlap: 100% airtight closure at 90°\n"
                "• Raised 0.4mm bottom sealing land & MG90S servo horn pocket\n"
                "• Outer tactile indicator tab confirms damper position"
            ),
            "box_xy": (0.04, 0.60),
            "arrow_start": (0.24, 0.50),
            "bg_color": '#fff7ed',
            "border_col": '#ea580c',
            "rad": 0.04
        },
        {
            "key": "fan",
            "title": "5015 Centrifugal Blower Fan",
            "desc": (
                "• 50 x 50 x 15 mm 12V DC brushless centrifugal fan\n"
                "• 4.5 CFM maximum airflow delivers crisp pit stoking\n"
                "• 20 x 15 mm exhaust discharges straight into lower plenum\n"
                "• Wires route internally up to rear RJ45 umbilical bay"
            ),
            "box_xy": (0.04, 0.32),
            "arrow_start": (0.24, 0.24),
            "bg_color": '#f1f5f9',
            "border_col": '#475569',
            "rad": -0.02
        },
        {
            "key": "plate",
            "title": "Part 5: vision_pro_kamado_slide_plate.stl",
            "desc": (
                "• Vision Pro Kamado S-Series draft door replacement slide\n"
                "• 195mm curvature radius, drops directly into grill track\n"
                "• 31.8mm (1-1/4\") female receiver port accepts BBQ Guru nozzle\n"
                "• Full mechanical interlock with zero grill modification"
            ),
            "box_xy": (0.74, 0.90),
            "arrow_start": (0.74, 0.81),
            "bg_color": '#fef2f2',
            "border_col": '#dc2626',
            "rad": -0.04
        },
        {
            "key": "cap",
            "title": "Part 3: rotodamper_cap.stl",
            "desc": (
                "• Upper collecting plenum chamber channels throttled air\n"
                "• BBQ Guru standard 31.5mm OD nozzle with 38mm stop collar\n"
                "• Dash-121 O-ring groove (2.8mm W x 1.4mm D) for airtight seal\n"
                "• Tethered 8P8C RJ45 umbilical socket pocket at rear\n"
                "• 4x M3 counterbored screw holes clamp to base"
            ),
            "box_xy": (0.74, 0.63),
            "arrow_start": (0.74, 0.57),
            "bg_color": '#f0f9ff',
            "border_col": '#0284c7',
            "rad": -0.04
        },
        {
            "key": "base",
            "title": "Part 1: rotodamper_base.stl",
            "desc": (
                "• Lower structural clam shell & lower air plenum\n"
                "• Divider deck with matching 80° fixed sector aperture\n"
                "• Recessed disc seat (49.6mm ID) with M2.5/M3 center pivot\n"
                "• Integrated 5015 blower bay with internal expansion duct"
            ),
            "box_xy": (0.74, 0.38),
            "arrow_start": (0.74, 0.31),
            "bg_color": '#f8fafc',
            "border_col": '#334155',
            "rad": -0.03
        },
        {
            "key": "fancov",
            "title": "Part 4: rotodamper_fan_cover.stl",
            "desc": (
                "• Bottom protective intake cover with aerodynamic louvers\n"
                "• Blocks embers, dust, and insects (>80% free airflow area)\n"
                "• 4x M3 countersunk screw holes secure fan to lower base"
            ),
            "box_xy": (0.74, 0.16),
            "arrow_start": (0.74, 0.13),
            "bg_color": '#f0fdf4',
            "border_col": '#16a34a',
            "rad": 0.03
        },
    ]

    for c in callouts:
        pt2d = get_2d(anchors[c["key"]])
        bx, by = c["box_xy"]
        ax_start = c["arrow_start"]

        # Callout Text Box
        full_text = f"{c['title']}\n{c['desc']}"
        fig.text(bx, by, full_text, fontsize=8.8, family='monospace', va='top', ha='left',
                 bbox=dict(boxstyle='round,pad=0.55', facecolor=c['bg_color'],
                           edgecolor=c['border_col'], lw=1.5, alpha=0.96))

        # Curved Leader Line to 3D Part
        ax.annotate(
            "",
            xy=(pt2d[0], pt2d[1]), xycoords='figure fraction',
            xytext=ax_start, textcoords='figure fraction',
            arrowprops=dict(
                arrowstyle="-|>", color=c['border_col'], lw=1.6,
                connectionstyle=f"arc3,rad={c['rad']}",
                mutation_scale=12
            )
        )

    # 12. Bill of Materials (BOM) Table at Canvas Bottom
    bom_headers = ["Item", "Part / Component", "Qty", "Material / Spec", "Function / Notes"]
    bom_data = [
        ["1", "rotodamper_base.stl", "1", "PETG / ABS (0.2mm layer, 4 walls)", "Lower plenum body, fixed 80° sector deck, 5015 fan bay"],
        ["2", "rotodamper_disc.stl", "1", "PETG / ABS (0.2mm layer, 100% infill)", "Rotary throttle disc, 80° aperture, raised sealing rim, servo hub"],
        ["3", "rotodamper_cap.stl", "1", "PETG / ABS (0.2mm layer, 4 walls)", "Upper plenum, BBQ Guru 31.5mm nozzle, MG90S bay, RJ45 port"],
        ["4", "rotodamper_fan_cover.stl", "1", "PETG / ABS (0.2mm layer)", "Bottom fan intake grille with debris louvers, wire pass-through"],
        ["5", "vision_pro_kamado_slide_plate.stl", "1", "PETG / ABS (0.2mm layer, heat-resistant)", "Curved slide plate dropping into Kamado Pro draft door track"],
        ["6", "5015 Blower Fan (12V)", "1", "12V DC, 4.5 CFM (50x50x15mm)", "Forced-draft combustion air supply via internal manifold"],
        ["7", "TowerPro MG90S Micro Servo", "1", "Metal Gear, 9g, 1.8-2.2 kg·cm", "Coaxial direct drive to damper disc (0-90° linear modulation)"],
        ["8", "Dash-121 Silicone O-Ring", "1", "Silicone/Viton (26.65mm ID x 2.38mm CS)", "Hermetic seal between BBQ Guru nozzle and slide plate receiver"],
        ["9", "RJ45 8P8C Breakout / Keystone", "1", "Standard Cat5/6 8-pin socket", "HeaterMeter-standard umbilical port for power & control signals"],
        ["10", "Fasteners (M3 Cap & CS, M2)", "1 set", "304 Stainless Steel", "4x M3x25mm cap screws, 4x M3x8mm CS, 1x M2.5 disc screw, 2x M2"]
    ]

    table_ax = fig.add_axes([0.05, 0.02, 0.65, 0.16])
    table_ax.axis('off')
    table = table_ax.table(cellText=bom_data, colLabels=bom_headers, loc='center', cellLoc='left',
                           colColours=['#e2e8f0']*5, colWidths=[0.05, 0.28, 0.06, 0.26, 0.35])
    table.auto_set_font_size(False)
    table.set_fontsize(7.8)
    table.scale(1.0, 1.25)
    for (row, col), cell in table.get_celld().items():
        if row == 0:
            cell.set_text_props(weight='bold', color='#0f172a')
        else:
            cell.set_text_props(color='#334155')

    # Output file paths
    img_dir = os.path.join(REPO_ROOT, "docs", "images")
    os.makedirs(img_dir, exist_ok=True)
    png_path = os.path.join(img_dir, "rotodamper_assembly_diagram.png")
    jpg_path = os.path.join(img_dir, "rotodamper_assembly_diagram.jpg")
    cad_jpg_path = os.path.join(REPO_ROOT, "cad", "rotodamper_assembly_diagram.jpg")

    print(f"Rendering high-resolution assembly diagram to {png_path}...")
    plt.savefig(png_path, dpi=200, bbox_inches='tight', facecolor='#ffffff')
    plt.close(fig)

    # Convert to RGB JPEG for universal compatibility
    print("Converting to high-quality JPEG...")
    im = Image.open(png_path).convert('RGB')
    im.save(jpg_path, 'JPEG', quality=95)
    im.save(cad_jpg_path, 'JPEG', quality=95)

    print(f"SUCCESS: Rendered assembly diagram:\n  -> {png_path}\n  -> {jpg_path}\n  -> {cad_jpg_path}")

if __name__ == '__main__':
    main()
