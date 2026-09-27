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
part = "all"; // [all: Assembled Preview, housing: Main Housing Body, rotor: Damper Rotor, output_adapter: Smoker Output Adapter, lid: Electronics Bay Lid, fan_cover: Blower Intake Grille]

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
// Nozzle cross-section geometry
output_shape = "round"; // [round: Cylindrical Pipe, rectangular: Rectangular Duct]

// Round output outer diameter (mm) (e.g. 25.4 for 1" NPT, 31.75 for 1.25", 38.1 for 1.5")
output_round_od = 25.4;
// Round output wall thickness (mm)
output_round_wall = 2.5;

// Rectangular output outer width (mm) (e.g. 50 for Kamado bottom slide vent)
output_rect_w = 50;
// Rectangular output outer height (mm)
output_rect_h = 30;
// Rectangular output wall thickness (mm)
output_rect_wall = 2.5;

// Insertion nozzle length extending into smoker vent (mm)
output_nozzle_len = 35;

// Enable mounting flange plate
include_flange = true;
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
    difference() {
        union() {
            // Adapter base mounting plate (attaches to housing boss)
            translate([0, 0, 0])
                rounded_box([damper_d + 16, fan_depth + wall, flange_t], 3);

            // Transition nozzle geometry
            if (output_shape == "round") {
                // Cylindrical pipe nozzle
                translate([(damper_d + 16)/2, (fan_depth + wall)/2, flange_t])
                    cylinder(d=output_round_od, h=output_nozzle_len);
                
                // Optional smoker mounting flange
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
            translate([(damper_d + 16)/2, (fan_depth + wall)/2, -1])
                cylinder(d=inner_d, h=output_nozzle_len + flange_t + 5);
        } else {
            inner_w = output_rect_w - 2*output_rect_wall;
            inner_h = output_rect_h - 2*output_rect_wall;
            translate([(damper_d + 16 - inner_w)/2, (fan_depth + wall - inner_h)/2, -1])
                cube([inner_w, inner_h, output_nozzle_len + flange_t + 5]);
        }

        // 4x M3 mounting screw holes into housing
        translate([(damper_d + 16)/2 - 14, (fan_depth + wall)/2 - 8, -1]) cylinder(d=3.4, h=flange_t + 2);
        translate([(damper_d + 16)/2 + 14, (fan_depth + wall)/2 - 8, -1]) cylinder(d=3.4, h=flange_t + 2);
        translate([(damper_d + 16)/2 - 14, (fan_depth + wall)/2 + 8, -1]) cylinder(d=3.4, h=flange_t + 2);
        translate([(damper_d + 16)/2 + 14, (fan_depth + wall)/2 + 8, -1]) cylinder(d=3.4, h=flange_t + 2);

        // Flange mounting screw holes (for smoker attachment bolts)
        if (include_flange) {
            cx = (damper_d + 16)/2;
            cy = (fan_depth + wall)/2;
            translate([cx - flange_hole_spacing_x/2, cy - flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
            translate([cx + flange_hole_spacing_x/2, cy - flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
            translate([cx - flange_hole_spacing_x/2, cy + flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
            translate([cx + flange_hole_spacing_x/2, cy + flange_hole_spacing_y/2, -1]) cylinder(d=flange_hole_d, h=100);
        }
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
} else if (part == "lid") {
    electronics_lid();
} else if (part == "fan_cover") {
    fan_cover();
}
