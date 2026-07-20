// ===========================================================================
// VT100-PI-ZERO  --  scaled-down DEC VT100-style enclosure for an LCD panel.
//
// A desktop "wedge" shell: vertical screen face, sloped hood, deep chin with a
// "vt100" deboss. The LCD mounts from behind onto four bolt-bosses; a cavity
// behind it takes the driver board + Pi Zero, with a cable exit at the rear.
//
// Everything is parametric -- fill in the MEASURE block from your screen and
// re-render. Units: millimetres. Built/tested on OpenSCAD 2021.01.
//
//   Preview:  F5      Render (for STL): F6      Export: File > Export > STL
// ===========================================================================

/* [What to render] */
part = "all";          // ["all":Whole case, "body":Shell only, "bezelcheck":Front face only]

/* ===================== MEASURE THESE ON YOUR SCREEN ===================== */
// ---- Lit image area (the actual picture, not the frame) ----
visible_w = 160;       // image width
visible_h = 120;       // image height   (VT100 was ~4:3)

// ---- LCD module ----
panel_lip   = 4;       // how far the glass/frame sits proud, front-to-back
board_depth = 24;      // clear depth needed behind the panel for board + cables + Pi

// ---- Mounting bolt centres (the "ears") ----
hole_dx = 176;         // horizontal centre-to-centre of the mount holes
hole_dy = 128;         // vertical   centre-to-centre of the mount holes
screw_d = 3.2;         // screw clearance (M3 = 3.2)
// If your panel mounts by its outer ears rather than tapped holes, just set
// hole_dx/hole_dy to those ear-hole spacings.

/* ===================== ENCLOSURE STYLING (taste) ===================== */
bezel_side   = 18;     // screen-to-edge margin, left/right
bezel_top    = 20;     // screen-to-edge margin, top
bezel_bottom = 40;     // chin height below the screen (DEC look: bigger)
wall         = 3;      // shell wall thickness
corner_r     = 7;      // soft moulded-edge radius
back_h_frac  = 0.70;   // rear height as a fraction of the front height (the wedge)
case_depth   = 120;    // overall front-to-back depth for the LOOK (~1/3 of a real
                       // VT100). The cavity only needs board_depth; this just sets
                       // how deep the wedge sits. Effective depth = max(the two).
recess_d     = 2.0;    // depth of the picture-frame recess around the screen
recess_lip   = 4;      // width of that recess step
boss_len     = 10;     // how far the mount bosses stand off the front wall
boss_d       = 9;      // mount boss outer diameter
badge        = "vt100";// chin deboss text ("" to omit)
badge_size   = 11;

/* ===================== DERIVED ===================== */
$fn = 56;
eps = 0.1;

front_w = max(visible_w + 2*bezel_side, hole_dx + boss_d + 2*wall);  // ensure bosses fit
front_h = bezel_top + visible_h + bezel_bottom;
body_w  = front_w;
body_d  = max(wall + board_depth + wall, case_depth);   // fit the board, or the looks
body_h  = front_h;
body_hb = front_h * back_h_frac;               // rear (short) height

screen_cz = bezel_bottom + visible_h/2;        // screen centre height, from base
badge_cz  = bezel_bottom/2;                    // chin text height

/* ===================== PRIMITIVES ===================== */

// Rounded wedge: tall vertical front face (y=0), sloping top down to a shorter
// back. Built as the hull of 8 corner spheres so every edge is softly moulded.
module rounded_wedge(w, d, hf, hb, r) {
    hull() for (x = [-(w/2 - r), (w/2 - r)]) {
        translate([x, r,     r     ]) sphere(r);   // front bottom
        translate([x, r,     hf - r ]) sphere(r);   // front top
        translate([x, d - r, r     ]) sphere(r);   // back bottom
        translate([x, d - r, hb - r ]) sphere(r);   // back top
    }
}

// A rectangular hole with rounded corners, bored along +Y (depth), centred in X/Z.
module screen_hole(w, h, depth, rr) {
    hull() for (x = [-1, 1], z = [-1, 1])
        translate([x*(w/2 - rr), 0, z*(h/2 - rr)])
            rotate([-90, 0, 0]) cylinder(h = depth, r = rr);
}

/* ===================== FEATURES ===================== */

module screen_cut() {
    // through-hole for the image
    translate([0, -eps, screen_cz])
        screen_hole(visible_w, visible_h, wall + 2*eps, 5);
    // shallow picture-frame recess on the outside
    translate([0, -eps, screen_cz])
        screen_hole(visible_w + 2*recess_lip, visible_h + 2*recess_lip, recess_d + eps, 7);
}

module mount_bosses() {
    for (x = [-hole_dx/2, hole_dx/2], z = [screen_cz - hole_dy/2, screen_cz + hole_dy/2])
        translate([x, wall, z]) rotate([-90, 0, 0])
            difference() {
                cylinder(h = boss_len, d = boss_d);
                translate([0, 0, -eps]) cylinder(h = boss_len + 2*eps, d = screw_d);
            }
}

// centred rear cable slot for HDMI / USB / power, low on the back wall
module cable_slot() {
    slot_w = 46; slot_h = 20;
    translate([-slot_w/2, body_d - wall - eps, wall + 6])
        cube([slot_w, wall + 2*eps, slot_h]);
}

module hood_vents() {
    // a row of slots cut into the sloping hood for airflow
    n = 7; sw = 3; sl = min(60, body_w - 4*bezel_side);
    for (i = [0 : n-1])
        translate([ (i - (n-1)/2) * (sw*2.2), body_d*0.45, body_h - 2 ])
            rotate([0,0,0])
            cube([sw, sl, wall*3], center = true);
}

module badge_deboss() {
    if (badge != "")
        translate([0, 0.8, badge_cz])
            rotate([90, 0, 0])
                linear_extrude(1.2)
                    text(badge, size = badge_size, halign = "center",
                         valign = "center", font = "Liberation Mono:style=Bold");
}

/* ===================== ASSEMBLY ===================== */

module shell() {
    difference() {
        rounded_wedge(body_w, body_d, body_h, body_hb, corner_r);
        // hollow it out
        translate([0, wall, wall])
            rounded_wedge(body_w - 2*wall, body_d - 2*wall,
                          body_h - 2*wall, body_hb - 2*wall, max(1, corner_r - wall));
        screen_cut();
        cable_slot();
        hood_vents();
        badge_deboss();
    }
    mount_bosses();
}

module bezelcheck() {
    // just the front slab, to eyeball screen/bezel/bolt alignment quickly
    difference() {
        translate([-body_w/2, 0, 0]) cube([body_w, wall, body_h]);
        screen_cut();
    }
    mount_bosses();
}

if      (part == "all")        shell();
else if (part == "body")       shell();
else if (part == "bezelcheck") bezelcheck();
