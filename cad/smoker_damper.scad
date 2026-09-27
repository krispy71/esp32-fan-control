// ====================================================================
// ESP32 Smoker Fan & Servo Damper Controller
// Parametric 3D Printable Housing & Airflow Regulator (OpenSCAD)
// ====================================================================
// Supports:
//   - Configurable blower fan sizes (5015, 7530, 9733, etc.)
//   - Servo-driven rotating barrel damper (MG90S / SG90)
//   - Parametric output connection (Round pipe or Rectangular flange)
//   - Dual Form Factors:
//       * Config A: All-in-one integrated pod with ESP32 & 5V electronics bay
//       * Config B: Slim tethered pod with RJ45 umbilical connection
// ====================================================================

/* [Render Part Selection] */
// Which component to generate
part = "all"; // [all: Assembled Preview, housing: Main Housing Body, rotor: Damper Rotor, output_adapter: Smoker Output Adapter, vision_pro_plate: Vision Pro Kamado Slide Plate, lid: Electronics Bay Lid, fan_cover: Blower Intake Grille]

/* [Packaging Configuration] */
// True = All-in-one integrated pod (houses ESP32 + 5V circuitry); False = Slim tethered pod (RJ45 port)
include_electronics_bay = true;
// Main chassis wall thickness (mm)
wall = 2.4;

/* [Blower Fan Parameters] */
// Blower outer diameter / body width (mm) (e.g. 50 for 5015, 75 for 7530, 97 for 9733)
fan_size = 50; 
// Blower thickness / axial depth (mm) (e.g. 15 for 5015, 30 for 7530, 33 for 9733)
fan_depth = 15;
// Fan exhaust throat width (mm)
fan_throat_w = 20;
// Fan exhaust throat height (mm)
fan_throat_h = 15;
// Fan intake circle diameter (mm)
fan_intake_d = 32;

/* [Damper Valve & Servo Parameters] */
// Damper barrel cylinder outer diameter (mm)
damper_d = 28;
// Damper chamber length (mm)
damper_len = 32;
// Radial tolerance between rotor and housing walls (mm)
rotor_clearance = 0.45;
// Preview damper aperture angle in degrees (0 = Closed, 90 = Fully Open)
preview_angle = 45; // [0:90]

// Micro-servo dimensions (MG90S / SG90 default)
servo_w = 12.4;
servo_l = 23.0;
servo_h = 24.5;
servo_flange_l = 32.5;
servo_shaft_offset = 6.0;

/* [Smoker Output Connection] */
// Preset smoker interface adapter
output_preset = "bbq_guru"; // [bbq_guru: BBQ Guru Vision Pro / Pit Viper Port (1.25" / 31.5mm with O-Ring), custom_round: Custom Round Pipe, custom_rect: Custom Rectangular Duct, npt_1: 1" NPT Pipe (25.4mm), npt_1_5: 1.5" Pipe (38.1mm), kamado_rect: Kamado Slide Door]

// Nozzle cross-section geometry (derived from preset or manual)
output_shape = (output_preset == "custom_rect" || output_preset == "kamado_rect") ? "rectangular" : "round";

// Round output outer diameter (mm)
// Note: 31.5mm is sized with 0.25mm printing tolerance to slip snugly into BBQ Guru 31.8mm (1-1/4") receiver ports
output_round_od = (output_preset == "bbq_guru") ? 31.5 :
                  (output_preset == "npt_1") ? 25.4 :
                  (output_preset == "npt_1_5") ? 38.1 : 31.5;

// Round output wall thickness (mm)
output_round_wall = 2.5;

// Insertion nozzle length extending into smoker vent / adapter tube (mm)
output_nozzle_len = (output_preset == "bbq_guru") ? 32.0 : 35.0;

// Enable O-Ring friction seal groove on cylindrical nozzle (standard for BBQ Guru Pit Viper fit)
include_oring_groove = (output_preset == "bbq_guru");
// O-ring groove distance from nozzle tip (mm)
oring_offset_from_tip = 10.0;
// O-ring groove width (mm) (2.8mm accommodates standard 3/32" / 2.5mm cross-section O-rings)
oring_groove_width = 2.8;
// O-ring groove depth (mm) (1.4mm depth leaves 28.7mm root OD for standard Dash-121 / Dash-122 silicone O-rings)
oring_groove_depth = 1.4;

// Enable stop shoulder collar at base of nozzle (rests flush against BBQ Guru adapter face)
include_stop_collar = (output_preset == "bbq_guru");
// Stop collar outer diameter (mm)
stop_collar_d = 38.0;
// Stop collar thickness (mm)
stop_collar_t = 3.0;

// Rectangular output outer width (mm) (e.g. 50 for Kamado bottom slide vent)
output_rect_w = 50;
// Rectangular output outer height (mm)
output_rect_h = 30;
// Rectangular output wall thickness (mm)
output_rect_wall = 2.5;

// Enable mounting flange plate (with bolt holes for custom smoker mounting)
include_flange = (output_preset != "bbq_guru");
// Flange plate outer width (mm)
flange_w = 70;
// Flange plate outer height (mm)
flange_h = 55;
// Flange plate thickness (mm)
flange_t = 3.5;
// Flange screw hole diameter (mm)
flange_hole_d = 4.2;
// Flange hole horizontal center-to-center spacing (mm)
flange_hole_spacing_x = 55;
// Flange hole vertical center-to-center spacing (mm)
flange_hole_spacing_y = 40;

/* [Vision Pro S-Series Slide Plate (Drop-in Replacement)] */
// Width of Vision Kamado Pro S-Series draft track (mm)
vision_plate_w = 82.0;
// Height of Vision Kamado Pro S-Series draft door (mm)
vision_plate_h = 74.0;
// Plate thickness (mm)
vision_plate_t = 2.5;
// Ceramic kamado body radius of curvature (mm)
vision_kamado_r = 195.0;
// Female blower receiver port inner diameter (mm) (matches BBQ Guru 31.8mm / 1-1/4")
vision_port_id = 31.8;
// Female blower receiver port outer diameter (mm)
vision_port_od = 36.8;
// Female receiver port depth (mm)
vision_port_len = 25.0;

/* [Integrated Electronics Bay (Config A)] */
// Electronics bay interior width (mm) (fits ESP32 + MAX31855 + MOSFET PCB)
bay_interior_w = 58;
// Electronics bay interior length (mm)
bay_interior_l = 68;
// Electronics bay interior depth (mm)
bay_interior_h = 22;

/* [Rendering Quality] */
$fn = 60;

// ====================================================================
// HELPER MODULES & MATH
// ====================================================================

// Rounded box utility
module rounded_box(size, radius, sides=true) {
    w = size[0]; l = size[1]; h = size[2];
    hull() {
        translate([radius, radius, 0]) cylinder(r=radius, h=h);
        translate([w-radius, radius, 0]) cylinder(r=radius, h=h);
        translate([w-radius, l-radius, 0]) cylinder(r=radius, h=h);
        translate([radius, l-radius, 0]) cylinder(r=radius, h=h);
    }
}

// ====================================================================
// 1. MAIN HOUSING BODY
// ====================================================================
module main_housing() {
    difference() {
        union() {
            // Fan mounting pocket body
            translate([0, 0, 0])
                cube([fan_size + 2*wall, fan_size + 2*wall, fan_depth + wall]);

            // Transition duct to damper
            translate([(fan_size + 2*wall - fan_throat_w - 2*wall)/2, fan_size + wall, 0])
                cube([fan_throat_w + 2*wall, 25, fan_depth + wall]);

            // Cylindrical damper housing sleeve
            translate([(fan_size + 2*wall)/2, fan_size + wall + 25 + damper_d/2, (fan_depth + wall)/2])
                rotate([0, 90, 0])
                cylinder(d=damper_d + 2*wall, h=damper_len + 2*wall, center=true);

            // Servo mounting bracket tower
            translate([(fan_size + 2*wall)/2 + (damper_len/2) + wall, fan_size + wall + 25 + damper_d/2, 0])
                servo_bracket_body();

            // Output adapter mounting boss (modular 4-bolt flange face)
            translate([(fan_size + 2*wall - (damper_d + 16))/2, fan_size + wall + 25 + damper_d + wall - 1, 0])
                cube([damper_d + 16, 8, fan_depth + wall]);

            // Optional Integrated Electronics Bay (Config A)
            if (include_electronics_bay) {
                translate([(fan_size + 2*wall - bay_interior_w - 2*wall)/2, -bay_interior_l - wall, 0])
                    electronics_bay_body();
            } else {
                // Config B: RJ45 jack recess mount block
                translate([(fan_size + 2*wall - 22)/2, -12, 0])
                    cube([22, 12, fan_depth + wall]);
            }
        }

        // --- CUTOUTS ---

        // Blower fan pocket cutout
        translate([wall, wall, wall])
            cube([fan_size, fan_size, fan_depth + 1]);

        // Fan intake circular cutout on bottom
        translate([wall + fan_size/2, wall + fan_size/2, -1])
            cylinder(d=fan_intake_d, h=wall + 2);

        // Fan mounting screw holes
        translate([wall + 4, wall + 4, -1]) cylinder(d=3.2, h=fan_depth + 5);
        translate([wall + fan_size - 4, wall + 4, -1]) cylinder(d=3.2, h=fan_depth + 5);
        translate([wall + 4, wall + fan_size - 4, -1]) cylinder(d=3.2, h=fan_depth + 5);

        // Airway throat channel from fan to damper
        translate([(fan_size + 2*wall - fan_throat_w)/2, wall + fan_size - 1, wall + (fan_depth - fan_throat_h)/2])
            cube([fan_throat_w, 28, fan_throat_h]);

        // Cylindrical damper bore for rotor
        translate([(fan_size + 2*wall)/2, fan_size + wall + 25 + damper_d/2, (fan_depth + wall)/2])
            rotate([0, 90, 0])
            cylinder(d=damper_d, h=damper_len + 0.2, center=true);

        // Airway outlet from damper to smoker
        translate([(fan_size + 2*wall - fan_throat_w)/2, fan_size + wall + 25 + damper_d/2, wall + (fan_depth - fan_throat_h)/2])
            cube([fan_throat_w, damper_d/2 + wall + 10, fan_throat_h]);

        // Servo cutout & screw holes in servo bracket
        translate([(fan_size + 2*wall)/2 + (damper_len/2) + wall, fan_size + wall + 25 + damper_d/2, 0])
            servo_bracket_cutouts();

        // 4x M3 mounting holes for modular output nozzle adapter
        translate([(fan_size + 2*wall)/2 - 14, fan_size + wall + 25 + damper_d + wall + 4, (fan_depth + wall)/2 - 8])
            rotate([90, 0, 0]) cylinder(d=2.9, h=15, center=true);
        translate([(fan_size + 2*wall)/2 + 14, fan_size + wall + 25 + damper_d + wall + 4, (fan_depth + wall)/2 - 8])
            rotate([90, 0, 0]) cylinder(d=2.9, h=15, center=true);
        translate([(fan_size + 2*wall)/2 - 14, fan_size + wall + 25 + damper_d + wall + 4, (fan_depth + wall)/2 + 8])
            rotate([90, 0, 0]) cylinder(d=2.9, h=15, center=true);
        translate([(fan_size + 2*wall)/2 + 14, fan_size + wall + 25 + damper_d + wall + 4, (fan_depth + wall)/2 + 8])
            rotate([90, 0, 0]) cylinder(d=2.9, h=15, center=true);

        // Cutout for RJ45 jack (Config B)
        if (!include_electronics_bay) {
            translate([(fan_size + 2*wall - 16)/2, -14, wall + (fan_depth - 14)/2])
                cube([16, 16, 14]);
        }
    }
}

// Servo bracket body and cutouts
module servo_bracket_body() {
    translate([0, -servo_w/2 - wall, 0])
        cube([servo_l + 10, servo_w + 2*wall, fan_depth + wall]);
}

module servo_bracket_cutouts() {
    // Servo body cavity
    translate([3, -servo_w/2, wall + (fan_depth - servo_h)/2])
        cube([servo_l + 0.5, servo_w, servo_h + 10]);

    // Servo shaft clearance opening into damper barrel
    translate([-wall - 2, 0, (fan_depth + wall)/2])
        rotate([0, 90, 0])
        cylinder(d=8.0, h=wall + 5);

    // Servo mounting ear screw holes
    translate([1.5, 0, (fan_depth + wall)/2 - 10])
        rotate([0, 90, 0]) cylinder(d=2.2, h=25);
    translate([1.5, 0, (fan_depth + wall)/2 + 10])
        rotate([0, 90, 0]) cylinder(d=2.2, h=25);
}

// Integrated Electronics Bay (Config A)
module electronics_bay_body() {
    difference() {
        cube([bay_interior_w + 2*wall, bay_interior_l + wall, bay_interior_h + wall]);
        
        // Interior cavity for ESP32 + Power & TC breakout
        translate([wall, wall, wall])
            cube([bay_interior_w, bay_interior_l - wall, bay_interior_h + 5]);

        // USB-C 5V power port cutout on side wall
        translate([-1, 15, wall + 4])
            cube([wall + 2, 10, 5]);

        // K-Type Thermocouple jack opening on end wall
        translate([15, -1, wall + 4])
            cube([18, wall + 2, 10]);

        // Wire pass-through slot into fan/servo area
        translate([bay_interior_w/2 - 6, bay_interior_l - 2, wall])
            cube([12, wall + 4, 8]);

        // Snap/screw corner posts for lid
        translate([wall + 4, wall + 4, bay_interior_h + wall - 8]) cylinder(d=2.5, h=10);
        translate([bay_interior_w - 4, wall + 4, bay_interior_h + wall - 8]) cylinder(d=2.5, h=10);
    }
}

// ====================================================================
// 2. DAMPER ROTOR (CYLINDRICAL BARREL APERTURE)
// ====================================================================
module damper_rotor() {
    r_dia = damper_d - 2*rotor_clearance;
    r_len = damper_len - 1.0;

    difference() {
        union() {
            // Main barrel cylinder
            cylinder(d=r_dia, h=r_len, center=true);

            // Servo horn coupler boss on drive side
            translate([0, 0, r_len/2])
                cylinder(d=12, h=4.0);
        }

        // Cross-bore airflow aperture (matches throat size)
        translate([0, 0, 0])
            cube([fan_throat_w, r_dia + 2, fan_throat_h], center=true);

        // Pocket to embed micro-servo arm / horn
        translate([0, 0, r_len/2 + 1.5])
            cylinder(d=8.2, h=3.0);
        translate([0, 0, r_len/2 + 1.0])
            cube([18.5, 4.8, 4.0], center=true);

        // Center screw hole to secure horn to servo spline
        translate([0, 0, r_len/2 - 8])
            cylinder(d=2.4, h=15);
    }
}

// ====================================================================
// 3. SMOKER OUTPUT ADAPTER (MODULAR BOLT-ON NOZZLE)
// ====================================================================
module output_adapter() {
    cx = (damper_d + 16)/2;
    cy = (fan_depth + wall)/2;

    difference() {
        union() {
            // Adapter base mounting plate (attaches to housing boss)
            translate([0, 0, 0])
                rounded_box([damper_d + 16, fan_depth + wall, flange_t], 3);

            // Transition nozzle geometry
            if (output_shape == "round") {
                // Optional mechanical stop collar (seats flush against BBQ Guru adapter face)
                if (include_stop_collar) {
                    translate([cx, cy, flange_t])
                        cylinder(d=stop_collar_d, h=stop_collar_t);
                }

                // Cylindrical pipe nozzle
                translate([cx, cy, flange_t])
                    cylinder(d=output_round_od, h=output_nozzle_len);
                
                // Optional bolt flange
                if (include_flange) {
                    translate([(damper_d + 16 - flange_w)/2, (fan_depth + wall - flange_h)/2, flange_t + 10])
                        rounded_box([flange_w, flange_h, flange_t], 4);
                }
            } else {
                // Rectangular duct nozzle
                translate([(damper_d + 16 - output_rect_w)/2, (fan_depth + wall - output_rect_h)/2, flange_t])
                    cube([output_rect_w, output_rect_h, output_nozzle_len]);
                
                // Optional smoker mounting flange
                if (include_flange) {
                    translate([(damper_d + 16 - flange_w)/2, (fan_depth + wall - flange_h)/2, flange_t + 10])
                        rounded_box([flange_w, flange_h, flange_t], 4);
                }
            }
        }

        // Central airflow bore through adapter plate and nozzle
        if (output_shape == "round") {
            inner_d = output_round_od - 2*output_round_wall;
            translate([cx, cy, -1])
                cylinder(d=inner_d, h=output_nozzle_len + flange_t + 5);

            // Tip lead-in chamfer for easy alignment into BBQ Guru port
            translate([cx, cy, flange_t + output_nozzle_len - 1.5])
                cylinder(d1=inner_d, d2=output_round_od + 0.5, h=1.6);

            // O-Ring retention groove cutout (standard BBQ Guru Pit Viper friction seal)
            if (include_oring_groove) {
                groove_z = flange_t + output_nozzle_len - oring_offset_from_tip;
                translate([cx, cy, groove_z])
                    difference() {
                        cylinder(d=output_round_od + 2, h=oring_groove_width);
                        translate([0, 0, -1])
                            cylinder(d=output_round_od - 2*oring_groove_depth, h=oring_groove_width + 2);
                    }
            }
        } else {
            inner_w = output_rect_w - 2*output_rect_wall;
            inner_h = output_rect_h - 2*output_rect_wall;
            translate([(damper_d + 16 - inner_w)/2, (fan_depth + wall - inner_h)/2, -1])
                cube([inner_w, inner_h, output_nozzle_len + flange_t + 5]);
        }

        // 4x M3 mounting screw holes into housing
        translate([cx - 14, cy - 8, -1]) cylinder(d=3.4, h=flange_t + 2);
        translate([cx + 14, cy - 8, -1]) cylinder(d=3.4, h=flange_t + 2);
        translate([cx - 14, cy + 8, -1]) cylinder(d=3.4, h=flange_t + 2);
        translate([cx + 14, cy + 8, -1]) cylinder(d=3.4, h=flange_t + 2);

        // Flange mounting screw holes (for smoker attachment bolts)
        if (include_flange) {
            translate([cx - flange_hole_spacing_x/2, cy - flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
            translate([cx + flange_hole_spacing_x/2, cy - flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
            translate([cx - flange_hole_spacing_x/2, cy + flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
            translate([cx + flange_hole_spacing_x/2, cy + flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
        }
    }
}

// ====================================================================
// 4. VISION PRO S-SERIES SLIDE DOOR REPLACEMENT PLATE
// ====================================================================
// Standalone 3D printable slide door for Vision Kamado Pro S-Series grills.
// Drops directly into the existing bottom draft door track, featuring a
// standardized BBQ Guru 31.8mm (1-1/4") female receiver port.
module vision_pro_slide_plate() {
    plate_angle = (vision_plate_w / (2 * 3.14159265 * vision_kamado_r)) * 360;

    difference() {
        union() {
            // Clean 2-manifold curved cylindrical shell
            rotate([0, 0, -plate_angle/2])
            rotate_extrude(angle=plate_angle)
                translate([vision_kamado_r, -vision_plate_h/2])
                square([vision_plate_t, vision_plate_h]);

            // Central female blower receiver port (BBQ Guru standard tube)
            // Embedded 2mm into plate for clean manifold union
            translate([vision_kamado_r - 2, 0, 0])
                rotate([0, 90, 0])
                cylinder(d=vision_port_od, h=vision_port_len + 2);

            // Pull handle / slide tab on edge for easy insertion/removal
            rotate([0, 0, -plate_angle/2 + 2])
            translate([vision_kamado_r - 1, -4, -vision_plate_h/4])
                cube([8, 8, vision_plate_h/2]);
        }

        // Central airflow opening through receiver port into firebox
        translate([vision_kamado_r - 10, 0, 0])
            rotate([0, 90, 0])
            cylinder(d=vision_port_id, h=vision_port_len + 20);

        // Kill plug keeper hole (3.5mm hole for lanyard/silicone tether)
        translate([vision_kamado_r + vision_plate_t + vision_port_len - 5, 0, vision_port_od/2 - 2])
            cylinder(d=3.5, h=10, center=true);
    }
}

// ====================================================================
// 4. ELECTRONICS BAY LID (CONFIG A)
// ====================================================================
module electronics_lid() {
    lid_w = bay_interior_w + 2*wall;
    lid_l = bay_interior_l + wall;
    lid_t = 2.0;

    difference() {
        union() {
            rounded_box([lid_w, lid_l, lid_t], 2);
            // Inner centering lip
            translate([wall + 0.3, wall + 0.3, lid_t])
                rounded_box([bay_interior_w - 0.6, bay_interior_l - wall - 0.6, 2.0], 1.5);
        }

        // Heat ventilation slits (directed away from smoker face)
        for (i = [0 : 5]) {
            translate([lid_w/2 - 18, 15 + i*7, -1])
                cube([36, 2.5, lid_t + 4]);
        }

        // Screw holes
        translate([wall + 4, wall + 4, -1]) cylinder(d=3.0, h=10);
        translate([lid_w - wall - 4, wall + 4, -1]) cylinder(d=3.0, h=10);
    }
}

// ====================================================================
// 5. FAN INTAKE COVER / GRILLE
// ====================================================================
module fan_cover() {
    cover_w = fan_size + 2*wall;
    cover_t = 2.0;

    difference() {
        cube([cover_w, cover_w, cover_t]);

        // Protective intake concentric rings/grille
        translate([cover_w/2, cover_w/2, -1]) {
            cylinder(d=fan_intake_d, h=cover_t + 2);
        }

        // Mounting holes matching fan
        translate([wall + 4, wall + 4, -1]) cylinder(d=3.4, h=cover_t + 2);
        translate([wall + fan_size - 4, wall + 4, -1]) cylinder(d=3.4, h=cover_t + 2);
        translate([wall + 4, wall + fan_size - 4, -1]) cylinder(d=3.4, h=cover_t + 2);
    }
    // Add protective grill crossbars
    translate([cover_w/2 - 1.5, cover_w/2 - fan_intake_d/2, 0])
        cube([3.0, fan_intake_d, cover_t]);
    translate([cover_w/2 - fan_intake_d/2, cover_w/2 - 1.5, 0])
        cube([fan_intake_d, 3.0, cover_t]);
}

// ====================================================================
// TOP-LEVEL DISPATCH & ASSEMBLED PREVIEW
// ====================================================================
if (part == "all") {
    // Assembled visual preview
    color("LightSlateGray", 0.85) main_housing();

    // Damper rotor preview inside sleeve rotated to preview_angle
    translate([(fan_size + 2*wall)/2, fan_size + wall + 25 + damper_d/2, (fan_depth + wall)/2])
        rotate([preview_angle, 0, 90])
        color("Tomato") damper_rotor();

    // Output adapter mounted on front
    translate([(fan_size + 2*wall - (damper_d + 16))/2, fan_size + wall + 25 + damper_d + wall + 6, 0])
        rotate([90, 0, 0])
        color("SteelBlue") output_adapter();

    // Electronics bay lid
    if (include_electronics_bay) {
        translate([(fan_size + 2*wall - bay_interior_w - 2*wall)/2, -bay_interior_l - wall, bay_interior_h + wall + 2])
            color("DarkSlateGray", 0.6) electronics_lid();
    }

} else if (part == "housing") {
    main_housing();
} else if (part == "rotor") {
    damper_rotor();
} else if (part == "output_adapter") {
    output_adapter();
} else if (part == "vision_pro_plate") {
    vision_pro_slide_plate();
} else if (part == "lid") {
    electronics_lid();
} else if (part == "fan_cover") {
    fan_cover();
}
