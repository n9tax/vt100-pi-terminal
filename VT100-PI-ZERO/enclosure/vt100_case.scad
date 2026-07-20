// ===========================================================================
// VT100-PI-ZERO  --  scaled-down DEC VT100-style enclosure for an LCD panel.
//
// Silhouette follows Michael Gardi's VT100 replica geometry, documented at
// cocoacrumbs.com/blog/2021-07-02-dec-vt100-enclosure/ (CC BY-NC-SA). The
// defining cues, taken from that model's own numbers:
//   * the FRONT FACE LEANS BACK ~11 deg  (side profile [[0,0],[31,161],...])
//   * broad side cheeks, screen centred; only a gentle ~1.5 deg width taper
//   * a deep body (depth ~= height), hood sloping to a slightly shorter back
//   * chin below the screen
// Rebuilt here as a single hollow solid that mounts our LCD from behind, with a
// board/Pi cavity and a rear cable exit. Units: mm. OpenSCAD 2021.01.
//
//   Preview: F5   Render (STL): F6.  Render an image with the screen filled:
//   openscad -D show_screen=true -o out.png vt100_case.scad
// ===========================================================================

/* [What to render] */
part = "all";          // ["all":Whole case, "bezelcheck":Front face only]
show_screen = false;   // preview only: fill the opening with a dark "screen"

/* ===================== MEASURE THESE ON YOUR SCREEN ===================== */
visible_w = 161;       // lit image area width  (not the frame)
visible_h = 122;       // lit image area height (VT100 was ~4:3)
panel_lip   = 4;       // how far the glass/frame sits proud, front-to-back
board_depth = 30;      // clear depth needed behind the panel for board + Pi + cables
hole_dx = 176;         // mount bolt centres, horizontal
hole_dy = 128;         // mount bolt centres, vertical
screw_d = 3.2;         // screw clearance (M3 = 3.2)

/* ===================== VT100 PROPORTIONS (from the reference) ============ */
bezel_side   = 56;     // screen-to-edge margin, left/right  (broad cheeks)
bezel_top    = 24;     // screen-to-edge margin, top
bezel_bottom = 28;     // chin height below the screen
face_lean    = 11;     // deg the front face leans back  (the VT100 posture)
side_taper   = 1.5;    // deg the width tapers inward toward the top (gentle)
back_h_frac  = 0.845;  // rear height / front height  (hood slope)
case_depth   = 185;    // front-to-back depth (cavity only needs board_depth;
                       // effective depth = max(the two))
wall         = 3;      // shell wall thickness
corner_r     = 8;      // soft moulded-edge radius
recess_d     = 2.5;    // depth of the picture-frame recess around the screen
recess_lip   = 5;      // width of that recess step
boss_len     = 12;     // how far the mount bosses stand off the front wall
boss_d       = 9;      // mount boss outer diameter
badge        = "vt100";// chin deboss text ("" to omit)
badge_size   = 12;

/* ===================== DERIVED ===================== */
$fn = 56;
eps = 0.1;

front_h  = bezel_top + visible_h + bezel_bottom;          // along-face height
front_wb = max(visible_w + 2*bezel_side, hole_dx + boss_d + 2*wall);   // bottom width
front_wt = front_wb - 2*front_h*tan(side_taper);          // top width (narrower)
lean     = front_h * tan(face_lean);                       // how far the top sits back
body_d   = max(wall + board_depth + wall, case_depth);
body_hb  = front_h * back_h_frac;                          // rear (short) height

screen_cz = bezel_bottom + visible_h/2;                    // screen centre, up the face
badge_cz  = bezel_bottom/2;

/* ===================== BODY ===================== */

// Rounded, leaning wedge: front face leans back by `lean`, tapers to `wt` at the
// top, hood slopes down to a shorter back. Hull of 8 corner spheres.
module vt_body(wb, wt, d, hf, hb, r, ln) {
    hull() for (s = [-1, 1]) {
        translate([s*(wb/2 - r), r,      r     ]) sphere(r);   // front bottom
        translate([s*(wt/2 - r), ln + r,  hf - r ]) sphere(r);   // front top (leaned back)
        translate([s*(wb/2 - r), d - r,  r     ]) sphere(r);   // back bottom
        translate([s*(wt/2 - r), d - r,  hb - r ]) sphere(r);   // back top
    }
}

// Put children onto the leaning front face (features are authored in a flat,
// vertical frame; this tilts them back to sit on the face).
module on_face() { rotate([-face_lean, 0, 0]) children(); }

/* ===================== FEATURES (flat frame, then on_face) ============== */

module screen_hole(w, h, depth, rr) {          // rounded rect bored along +Y
    hull() for (x = [-1, 1], z = [-1, 1])
        translate([x*(w/2 - rr), 0, z*(h/2 - rr)])
            rotate([-90, 0, 0]) cylinder(h = depth, r = rr);
}

module screen_cut() {
    translate([0, -eps, screen_cz])                          // image through-hole
        screen_hole(visible_w, visible_h, 3*wall, 5);
    translate([0, -eps, screen_cz])                          // picture-frame recess
        screen_hole(visible_w + 2*recess_lip, visible_h + 2*recess_lip, recess_d + eps, 8);
}

// Two full-width bars across the inner face (one above, one below the screen),
// each spanning wall-to-wall so they fuse to the shell, with the mount holes
// drilled through. Robust vs. floating bosses on a leaning inner wall.
module mount_bars() {
    bh = boss_d;                                    // bar height (up the face)
    for (z = [screen_cz - hole_dy/2, screen_cz + hole_dy/2]) {
        fw = front_wb - (front_wb - front_wt) * (z/front_h);   // face width at this height
        barW = fw - 2*wall + 4;                     // +2mm each side embeds into the walls
        difference() {
            translate([-barW/2, 1.0, z - bh/2]) cube([barW, boss_len + wall, bh]);
            for (x = [-hole_dx/2, hole_dx/2])
                translate([x, 1.0 - eps, z]) rotate([-90, 0, 0])
                    cylinder(h = boss_len + wall + 2*eps, d = screw_d);
        }
    }
}

module badge_deboss() {
    if (badge != "")
        translate([0, 0.8, badge_cz]) rotate([90, 0, 0])
            linear_extrude(1.2)
                text(badge, size = badge_size, halign = "center", valign = "center",
                     font = "Liberation Mono:style=Bold");
}

module screen_fill() {                          // preview-only dark panel
    color([0.05, 0.06, 0.05])
        on_face() translate([0, wall - 0.6, screen_cz])
            screen_hole(visible_w - 1, visible_h - 1, 0.8, 5);
}

/* ---- body-frame features (not on the leaning face) ---- */

module cable_slot() {                           // rear exit, low centre
    w = 46; h = 20;
    translate([-w/2, body_d - wall - eps, wall + 8]) cube([w, wall + 2*eps, h]);
}

vent_n = 11;                                    // number of hood vent slots (0 = none)
module hood_vents() {                           // slots across the sloping hood
    n = vent_n; sw = 3.5; sl = 70;
    for (i = [0 : n-1])
        translate([(i - (n-1)/2) * (sw*2.3), body_d*0.55, front_h*0.9])
            rotate([90 - face_lean*1.6, 0, 0]) cube([sw, sl, wall*5], center = true);
}

/* ===================== ASSEMBLY ===================== */

module shell() {
    difference() {
        vt_body(front_wb, front_wt, body_d, front_h, body_hb, corner_r, lean);
        translate([0, wall, wall])                           // hollow
            vt_body(front_wb - 2*wall, front_wt - 2*wall, body_d - 2*wall,
                    front_h - 2*wall, body_hb - 2*wall, max(1, corner_r - wall), lean);
        on_face() screen_cut();
        on_face() badge_deboss();
        cable_slot();
        hood_vents();
    }
    on_face() mount_bars();
    if (show_screen) screen_fill();
}

module bezelcheck() {                            // leaning front slab only
    difference() {
        hull() for (s = [-1, 1]) {
            translate([s*(front_wb/2 - corner_r), corner_r, corner_r]) sphere(corner_r);
            translate([s*(front_wt/2 - corner_r), lean + corner_r, front_h - corner_r]) sphere(corner_r);
            translate([s*(front_wb/2 - corner_r), wall + 2*corner_r, corner_r]) sphere(corner_r);
            translate([s*(front_wt/2 - corner_r), lean + wall + 2*corner_r, front_h - corner_r]) sphere(corner_r);
        }
        on_face() screen_cut();
    }
    on_face() mount_bars();
    if (show_screen) screen_fill();
}

if   (part == "bezelcheck") bezelcheck();
else                        shell();
