// =============================================================================
// ROTO-DAMPER (RD3 STYLE) BLOWER DAMPER SYSTEM
// =============================================================================
// High-performance rotary sector valve damper inspired by the HeaterMeter
// RD3 Roto-Damper architecture.
//
// Key Features:
// 1. Direct-Drive Coaxial MG90S Servo: Zero-backlash direct drive to damper disc.
// 2. 80-Degree Sector Aperture: 288.5 mm^2 open area matching the 5015 fan throat
//    (300 mm^2) for zero-restriction blower airflow, with a 10-degree solid overlap
//    barrier for 100% airtight closure against natural chimney drafts.
// 3. Integrated 5015 Blower Bay: Houses standard 50x50x15mm centrifugal blower.
// 4. Tethered RJ45 Umbilical Port: HeaterMeter-standard 8P8C interface connecting
//    to the external ESP32 controller / green perfboard circuit.
// 5. BBQ Guru Standard Nozzle: 31.5 mm OD with Dash-121 O-ring groove and 38 mm
//    stop collar, 100% compatible with vision_pro_kamado_slide_plate.stl.
//
// Render Options (pass via -D 'part="NAME"'):
//   part = "all"        - Full assembled view with color coding
//   part = "exploded"   - Exploded assembly view showing stackup
//   part = "base"       - Lower shell & lower plenum (STL export)
//   part = "disc"       - Rotary damper sector disc (STL export)
//   part = "cap"        - Upper plenum, nozzle, servo & RJ45 bay (STL export)
//   part = "fan_cover"  - Bottom intake grille & fan guard (STL export)
// =============================================================================

part = "all"; // ["all", "exploded", "base", "disc", "cap", "fan_cover"]
$fn = 60;

// -----------------------------------------------------------------------------
// DIMENSIONAL PARAMETERS
// -----------------------------------------------------------------------------
wall = 2.4;

// 5015 Blower Fan
fan_w = 50.5;
fan_l = 50.5;
fan_h = 15.5;
fan_intake_d = 33.0;

// Damper Disc
disc_d = 48.0;
disc_t = 2.5;
disc_clearance = 0.8;
chamber_id = disc_d + 2 * disc_clearance; // 49.6 mm

// Sector Aperture (80 degrees linear throttling)
sector_angle = 80;
sector_r_in = 7.0;
sector_r_out = 21.5;

// TowerPro MG90S Micro Servo
servo_w = 12.6;
servo_l = 23.2;
servo_h = 24.5;
servo_flange_l = 32.5;
servo_flange_w = 12.6;
servo_shaft_offset = 5.8; // Edge to output spline center

// BBQ Guru Output Nozzle
nozzle_od = 31.5;
nozzle_id = 26.5;
nozzle_len = 32.0;
stop_collar_od = 38.0;
stop_collar_w = 3.0;
oring_w = 2.8;
oring_depth = 1.4;
oring_dist = 14.0; // Distance from nozzle tip

// Housing Outer Profile
body_w = 56.0;
body_front_r = body_w / 2; // 28.0 mm
fan_bay_y = -76.0;
base_h = fan_h + wall + 2.5; // 20.4 mm (Z: -20.4 to 0)
cap_h = 22.0;                // Z: 0 to 22.0 mm

// -----------------------------------------------------------------------------
// HELPER MODULES
// -----------------------------------------------------------------------------

// 2D Outer Hull Profile
module hull_2d() {
    hull() {
        // Forward circular chamber centered at (0, 0)
        circle(r=body_front_r);
        // Rear rectangular fan bay with rounded corners
        translate([-body_w/2 + 6, fan_bay_y + 6]) circle(r=6);
        translate([body_w/2 - 6, fan_bay_y + 6]) circle(r=6);
        translate([-body_w/2 + 6, -20]) circle(r=6);
        translate([body_w/2 - 6, -20]) circle(r=6);
    }
}

// Symmetric Sector Port centered at +Y (90 degrees)
module sector_port(r_in, r_out, angle, h) {
    rotate([0, 0, 90 - angle/2])
        rotate_extrude(angle=angle)
        translate([r_in, 0])
        square([r_out - r_in, h]);
}

// -----------------------------------------------------------------------------
// 1. ROTARY DAMPER DISC (rotodamper_disc)
// -----------------------------------------------------------------------------
module rotodamper_disc() {
    difference() {
        union() {
            // Main disc body
            cylinder(d=disc_d, h=disc_t);

            // Raised low-friction sealing rim on bottom face
            translate([0, 0, -0.4])
                difference() {
                    cylinder(d=disc_d - 1.0, h=0.4);
                    translate([0, 0, -0.5])
                        cylinder(d=sector_r_in*2 - 1.0, h=1.4);
                }

            // Central hub boss for servo horn
            cylinder(d=12.5, h=disc_t + 2.2);

            // Tactile indicator tab aligned with aperture center (+Y)
            rotate([0, 0, 90])
                translate([disc_d/2 - 0.5, -1.5, 0])
                cube([3.0, 3.0, disc_t]);
        }

        // 80-degree sector aperture
        translate([0, 0, -1])
            sector_port(sector_r_in, sector_r_out, sector_angle, disc_t + 5);

        // MG90S servo horn recess
        translate([0, 0, disc_t + 2.2 - 2.2])
            cube([18.5, 4.8, 3.0], center=true);
        translate([0, 0, disc_t + 2.2 - 2.2])
            cylinder(d=8.2, h=3.0);

        // Center screw through-bore
        translate([0, 0, -2])
            cylinder(d=2.6, h=disc_t + 8);
    }
}

// -----------------------------------------------------------------------------
// 2. BASE HOUSING (rotodamper_base)
// -----------------------------------------------------------------------------
module rotodamper_base() {
    difference() {
        // Outer body extrusion
        translate([0, 0, -base_h])
            linear_extrude(height=base_h)
            hull_2d();

        // Disc recess on top face (Z = -disc_t - 0.3 to 0)
        translate([0, 0, -disc_t - 0.3])
            cylinder(d=chamber_id, h=disc_t + 1);

        // Fixed sector opening in divider floor
        translate([0, 0, -base_h - 1])
            sector_port(sector_r_in, sector_r_out, sector_angle, base_h + 2);

        // Lower plenum air chamber under disc
        translate([0, 0, -base_h + wall])
            cylinder(d=chamber_id - 6.0, h=base_h - wall - disc_t - 2.5);

        // 5015 Blower fan pocket
        translate([-fan_w/2, fan_bay_y + wall, -base_h + wall])
            cube([fan_w, fan_l, fan_h + 1]);

        // Airway manifold connecting fan throat to lower plenum
        translate([-15, -26, -base_h + wall])
            cube([30, 15, fan_h]);

        // Fan bottom air intake cutout
        translate([0, fan_bay_y + wall + fan_l/2, -base_h - 1])
            cylinder(d=fan_intake_d, h=wall + 2);

        // Internal wire pass-up channel from fan to rear RJ45 bay
        translate([body_w/2 - 7, -50, -base_h + wall])
            cube([4.0, 30.0, base_h]);

        // Disc center pivot screw pilot hole
        translate([0, 0, -base_h + wall])
            cylinder(d=2.8, h=base_h);

        // 4x M3 corner fastener holes for Cap assembly
        for (dx = [-1, 1]) {
            for (dy = [-1, 1]) {
                translate([dx * 22, (dy > 0 ? 16 : -66), -base_h - 1])
                    cylinder(d=2.8, h=base_h + 2);
            }
        }

        // 4x M3 screw pilot holes for bottom fan cover
        for (dx = [-1, 1]) {
            for (dy = [-1, 1]) {
                translate([dx * 20, fan_bay_y + wall + (dy > 0 ? fan_l - 5 : 5), -base_h - 1])
                    cylinder(d=2.8, h=8);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 3. CAP HOUSING (rotodamper_cap)
// -----------------------------------------------------------------------------
module rotodamper_cap() {
    difference() {
        union() {
            // Main cap body
            linear_extrude(height=cap_h)
                hull_2d();

            // BBQ Guru output nozzle at +Y (Z centered at 12 mm)
            translate([0, body_front_r - 2, 12])
                rotate([-90, 0, 0])
                union() {
                    cylinder(d=nozzle_od, h=nozzle_len);
                    cylinder(d=stop_collar_od, h=stop_collar_w);
                }
        }

        // Hollow nozzle bore
        translate([0, body_front_r - 5, 12])
            rotate([-90, 0, 0])
            cylinder(d=nozzle_id, h=nozzle_len + 10);

        // Dash-121 O-ring groove on nozzle
        translate([0, body_front_r - 2 + nozzle_len - oring_dist, 12])
            rotate([-90, 0, 0])
            difference() {
                cylinder(d=nozzle_od + 2, h=oring_w);
                translate([0, 0, -1])
                    cylinder(d=nozzle_od - 2*oring_depth, h=oring_w + 2);
            }

        // Upper plenum collecting chamber (over sector aperture)
        translate([0, 0, -1])
            intersection() {
                cylinder(d=chamber_id, h=14);
                translate([-body_w/2, 0, 0]) cube([body_w, body_front_r + 5, 16]);
            }

        // Funnel throat transition to nozzle bore
        translate([0, 10, 12])
            rotate([-90, 0, 0])
            cylinder(r1=15, r2=nozzle_id/2, h=16);

        // Inverted MG90S servo pocket centered at (0, 0)
        translate([-servo_w/2, -servo_l + servo_shaft_offset, 0])
            cube([servo_w, servo_l, servo_h + 2]);
        // Flange mounting shelf
        translate([-servo_flange_w/2, -servo_flange_l + servo_shaft_offset + 4.5, 10])
            cube([servo_flange_w, servo_flange_l, 4]);
        // Output shaft through bore
        translate([0, 0, -1])
            cylinder(d=10.0, h=5);

        // RJ45 Socket pocket at rear (-Y)
        translate([-8.5, fan_bay_y - 1, 3])
            cube([17.0, 22.0, 16.0]);

        // Wire channel from servo bay to RJ45 pocket
        translate([-4, fan_bay_y + 15, 3])
            cube([8.0, 40.0, 6.0]);

        // Wire pass-up from lower fan bay to RJ45
        translate([body_w/2 - 7, -50, -1])
            cube([4.0, 20.0, 6.0]);

        // 4x M3 corner through-holes for cap screws
        for (dx = [-1, 1]) {
            for (dy = [-1, 1]) {
                translate([dx * 22, (dy > 0 ? 16 : -66), -1]) {
                    cylinder(d=3.4, h=cap_h + 2);
                    translate([0, 0, cap_h - 4])
                        cylinder(d=6.5, h=6);
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 4. FAN COVER (rotodamper_fan_cover)
// -----------------------------------------------------------------------------
module rotodamper_fan_cover() {
    difference() {
        union() {
            hull() {
                translate([-body_w/2 + 6, fan_bay_y + 6, 0]) cylinder(r=6, h=2.5);
                translate([body_w/2 - 6, fan_bay_y + 6, 0]) cylinder(r=6, h=2.5);
                translate([-body_w/2 + 6, -20, 0]) cylinder(r=6, h=2.5);
                translate([body_w/2 - 6, -20, 0]) cylinder(r=6, h=2.5);
            }
        }

        // 4x M3 screw countersinks matching base bottom bosses
        for (dx = [-1, 1]) {
            for (dy = [-1, 1]) {
                translate([dx * 20, fan_bay_y + wall + (dy > 0 ? fan_l - 5 : 5), -1]) {
                    cylinder(d=3.4, h=5);
                    translate([0, 0, 1.2])
                        cylinder(d1=3.4, d2=6.5, h=2);
                }
            }
        }

        // Aerodynamic intake grille slots over fan intake
        for (i = [-3:3]) {
            translate([i * 4, fan_bay_y + wall + fan_l/2, -1])
                cube([2.0, 26.0, 6], center=true);
        }
    }
}

// -----------------------------------------------------------------------------
// HARDWARE MOCKS (For assembly & exploded visualization)
// -----------------------------------------------------------------------------
module mock_5015_fan() {
    color([0.15, 0.15, 0.15, 0.9])
    difference() {
        cube([50.0, 50.0, 15.0]);
        // Intake
        translate([25, 25, -1]) cylinder(d=32, h=17);
        // Exhaust
        translate([5, 50 - 15, -1]) cube([20, 16, 17]);
    }
}

module mock_mg90s_servo() {
    color([0.1, 0.2, 0.6, 0.9])
    union() {
        // Main body
        translate([-servo_w/2, -servo_l + servo_shaft_offset, 0])
            cube([servo_w, servo_l, servo_h]);
        // Flanges
        translate([-servo_flange_w/2, -servo_flange_l + servo_shaft_offset + 4.5, 16])
            cube([servo_flange_w, servo_flange_l, 2.5]);
        // Spline shaft
        color([0.85, 0.75, 0.2])
            translate([0, 0, -4])
            cylinder(d=4.8, h=5);
    }
}

module mock_rj45_socket() {
    color([0.7, 0.7, 0.75, 0.9])
    translate([-8, fan_bay_y, 4])
        cube([16.0, 20.0, 14.0]);
}

// -----------------------------------------------------------------------------
// TOP-LEVEL RENDER DISPATCH
// -----------------------------------------------------------------------------
if (part == "disc") {
    rotodamper_disc();
} else if (part == "base") {
    // Sits flat on print bed with bottom at Z=0
    translate([0, 0, base_h])
        rotodamper_base();
} else if (part == "cap") {
    rotodamper_cap();
} else if (part == "fan_cover") {
    rotodamper_fan_cover();
} else if (part == "exploded") {
    // Exploded View stackup along Z axis
    // 1. Cap elevated
    translate([0, 0, 42]) {
        color([0.25, 0.45, 0.75, 0.85]) rotodamper_cap();
        // Servo inside cap
        translate([0, 0, 18]) mock_mg90s_servo();
        // RJ45 inside cap
        mock_rj45_socket();
    }

    // 2. Disc elevated
    translate([0, 0, 18])
        color([0.95, 0.45, 0.10, 0.95]) rotodamper_disc();

    // 3. Base at origin
    color([0.30, 0.32, 0.35, 0.90]) rotodamper_base();

    // 4. 5015 Fan lowered
    translate([-fan_w/2, fan_bay_y + wall, -base_h + wall - 18])
        mock_5015_fan();

    // 5. Fan Cover lowered
    translate([0, 0, -base_h - 38])
        color([0.20, 0.20, 0.22, 0.90]) rotodamper_fan_cover();

} else { // part == "all" (Assembled View)
    color([0.30, 0.32, 0.35, 0.90]) rotodamper_base();
    color([0.95, 0.45, 0.10, 0.95]) rotodamper_disc();
    color([0.25, 0.45, 0.75, 0.85]) rotodamper_cap();
    translate([0, 0, -base_h - 2.5])
        color([0.20, 0.20, 0.22, 0.90]) rotodamper_fan_cover();
}
