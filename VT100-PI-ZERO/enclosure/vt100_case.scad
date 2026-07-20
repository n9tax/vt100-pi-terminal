// ===========================================================================
// VT100-PI-ZERO  --  scaled-down DEC VT100-style enclosure for an LCD panel.
//
// Captures the VT100 "look": a front panel that is WIDER THAN TALL with broad
// side cheeks, a trapezoid that TAPERS INWARD toward the top (~3 deg/side), a
// recessed screen, a chin with a "vt100" deboss, and a hood that slopes back to
// a shorter, rounded rear. Proportions follow Michael Gardi's 60%-scale replica
// / the Cocoacrumbs reference (~274 x 174 front for a 161 x 122 screen).
//
// The LCD mounts from behind onto four bolt-bosses; the cavity behind takes the
// driver board + Pi Zero, with a rear cable exit. Everything is parametric --
// fill in the MEASURE block and re-render. Units: mm. OpenSCAD 2021.01.
//
//   Preview: F5   Render (for STL): F6   Export: File > Export > STL
// ===========================================================================

/* [What to render] */
part = "all";          // ["all":Whole case, "bezelcheck":Front slab only]

/* ===================== MEASURE THESE ON YOUR SCREEN ===================== */
// ---- Lit image area (the actual picture, not the frame) ----
visible_w = 161;       // image width
visible_h = 122;       // image height   (VT100 was ~4:3)

// ---- LCD module ----
panel_lip   = 4;       // how far the glass/frame sits proud, front-to-back
board_depth = 24;      // clear depth needed behind the panel for board + cables + Pi

// ---- Mounting bolt centres (the "ears") ----
hole_dx = 176;         // horizontal centre-to-centre of the mount holes
hole_dy = 128;         // vertical   centre-to-centre of the mount holes
screw_d = 3.2;         // screw clearance (M3 = 3.2)

/* ===================== ENCLOSURE STYLING (VT100 look) ===================== */
bezel_side   = 50;     // screen-to-edge margin, left/right  (VT100 has broad cheeks)
bezel_top    = 24;     // screen-to-edge margin, top
bezel_bottom = 30;     // chin height below the screen
side_taper   = 3;      // degrees each side tapers inward toward the top (trapezoid)
back_taper   = 6;      // extra mm each side the body narrows front-to-back
wall         = 3;      // shell wall thickness
corner_r     = 8;      // soft moulded-edge radius
back_h_frac  = 0.82;   // rear height as a fraction of front height (gentle hood slope)
case_depth   = 155;    // front-to-back depth for the LOOK (cavity only needs
                       // board_depth; effective depth = max(the two)).
recess_d     = 2.5;    // depth of the picture-frame recess around the screen
recess_lip   = 5;      // width of that recess step
boss_len     = 10;     // how far the mount bosses stand off the front wall
boss_d       = 9;      // mount boss outer diameter
badge        = "vt100";// chin deboss text ("" to omit)
badge_size   = 12;

/* ===================== DERIVED ===================== */
$fn = 56;
eps = 0.1;

front_h  = bezel_top + visible_h + bezel_bottom;               // front height
front_wb = max(visible_w + 2*bezel_side, hole_dx + boss_d + 2*wall);  // bottom width
top_inset = front_h * tan(side_taper);                         // per-side inward taper
front_wt = front_wb - 2*top_inset;                             // top width (narrower)

body_d  = max(wall + board_depth + wall, case_depth);
body_hb = front_h * back_h_frac;                               // rear (short) height

screen_cz = bezel_bottom + visible_h/2;                        // screen centre height
badge_cz  = bezel_bottom/2;

// front-face half-width at a given height z (accounts for the taper)
function halfw(z) = (front_wb/2) - (front_wb - front_wt)/2 * (z/front_h);

/* ===================== PRIMITIVES ===================== */

// Rounded, tapered wedge: trapezoid front face (wide base, narrow top) at y=0,
// hood sloping down to a shorter, slightly narrower rounded back. Hull of 8
// corner spheres so every edge is softly moulded.
module rounded_wedge(wb, wt, d, hf, hb, r) {
    hull() {
        // front face (y = r)
        for (s = [-1, 1]) {
            translate([s*(wb/2 - r), r, r     ]) sphere(r);   // bottom
            translate([s*(wt/2 - r), r, hf - r ]) sphere(r);   // top
        }
        // back face (y = d - r), narrowed by back_taper and shorter
        for (s = [-1, 1]) {
            translate([s*(wb/2 - back_taper - r), d - r, r     ]) sphere(r);   // bottom
            translate([s*(wt/2 - back_taper - r), d - r, hb - r ]) sphere(r);   // top
        }
    }
}

// Rectangular hole with rounded corners, bored along +Y, centred in X/Z.
module screen_hole(w, h, depth, rr) {
    hull() for (x = [-1, 1], z = [-1, 1])
        translate([x*(w/2 - rr), 0, z*(h/2 - rr)])
            rotate([-90, 0, 0]) cylinder(h = depth, r = rr);
}

/* ===================== FEATURES ===================== */

module screen_cut() {
    translate([0, -eps, screen_cz])                            // image through-hole
        screen_hole(visible_w, visible_h, wall + 2*eps, 5);
    translate([0, -eps, screen_cz])                            // picture-frame recess
        screen_hole(visible_w + 2*recess_lip, visible_h + 2*recess_lip, recess_d + eps, 8);
}

module mount_bosses() {
    for (x = [-hole_dx/2, hole_dx/2], z = [screen_cz - hole_dy/2, screen_cz + hole_dy/2])
        translate([x, wall, z]) rotate([-90, 0, 0])
            difference() {
                cylinder(h = boss_len, d = boss_d);
                translate([0, 0, -eps]) cylinder(h = boss_len + 2*eps, d = screw_d);
            }
}

module cable_slot() {                                          // rear exit, low centre
    slot_w = 46; slot_h = 20;
    translate([-slot_w/2, body_d - wall - eps, wall + 6])
        cube([slot_w, wall + 2*eps, slot_h]);
}

module hood_vents() {                                          // slots in the sloping hood
    n = 9; sw = 3.5; sl = 55;
    for (i = [0 : n-1])
        translate([(i - (n-1)/2) * (sw*2.4), body_d*0.55, front_h - 6])
            rotate([28, 0, 0]) cube([sw, sl, wall*4], center = true);
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
        rounded_wedge(front_wb, front_wt, body_d, front_h, body_hb, corner_r);
        translate([0, wall, wall])                             // hollow
            rounded_wedge(front_wb - 2*wall, front_wt - 2*wall, body_d - 2*wall,
                          front_h - 2*wall, body_hb - 2*wall, max(1, corner_r - wall));
        screen_cut();
        cable_slot();
        hood_vents();
        badge_deboss();
    }
    mount_bosses();
}

module bezelcheck() {                                          // front slab for quick checks
    difference() {
        hull() for (s = [-1, 1]) {
            translate([s*(front_wb/2 - corner_r), 0, corner_r]) cylinder(r = corner_r, h = wall);
            translate([s*(front_wt/2 - corner_r), 0, front_h - corner_r]) cylinder(r = corner_r, h = wall);
        }
        screen_cut();
    }
    mount_bosses();
}

if      (part == "bezelcheck") bezelcheck();
else                           shell();
