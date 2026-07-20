// ===========================================================================
// VT100-PI-ZERO  --  scaled-down DEC VT100 enclosure for an LCD panel.
//
// Built from Michael Gardi's VT100 replica geometry
// (cocoacrumbs.com/blog/2021-07-02-dec-vt100-enclosure/, CC BY-NC-SA). Unlike a
// rounded-box approximation, this uses the reference's actual FLAT-PANEL shape:
// the body is the real side profile  [[0,0],[31,161],[211,136],[211,0]]  --
// front face leaning back ~11 deg, gentle hood to a shorter back -- extruded
// across the width and trimmed to the ~1.5 deg trapezoid front. Crisp panels,
// lightly rounded edges, not a pillow.
//
// Single hollow shell: LCD mounts from behind, board/Pi cavity, open rear for
// access. Units: mm. OpenSCAD 2021.01.
//   Preview F5 · Render(STL) F6 · image: openscad -D show_screen=true -o o.png ...
// ===========================================================================

/* [What to render] */
part = "all";          // ["all":Whole case,"bezelcheck":Front slab only]
show_screen = false;   // preview only: fill the opening with a dark screen

/* ===================== MEASURE THESE ===================== */
visible_w = 161;       // lit image area width  (not the frame)
visible_h = 122;       // lit image area height
board_depth = 40;      // clear depth behind the panel for board + Pi + cables
panel_lip   = 4;       // how far the glass/frame sits proud
hole_dx = 176;         // mount bolt centres, horizontal
hole_dy = 128;         // mount bolt centres, vertical
screw_d = 3.2;         // screw clearance (M3 = 3.2)

/* ===================== VT100 PROPORTIONS (reference) ===================== */
bezel_side   = 56.5;   // screen-to-edge, L/R  (274 wide face for a 161 screen)
bezel_top    = 20;     // screen-to-edge, top
bezel_bottom = 27;     // chin
face_lean    = 11;     // deg the front face leans back
side_taper   = 1.5;    // deg width taper toward the top
back_h_frac  = 0.845;  // rear height / front height
case_depth   = 190;    // front-to-back depth
wall         = 3;
edge_r       = 6;      // panel edge rounding (reference CaseRounding)
recess_d     = 3;      // screen recess depth
recess_lip   = 6;      // recess frame width
badge        = "vt100";
badge_size   = 12;

/* ===================== DERIVED ===================== */
$fn = 64;
eps = 0.1;

front_h  = bezel_top + visible_h + bezel_bottom;
front_wb = max(visible_w + 2*bezel_side, hole_dx + 20);      // bottom width
front_wt = front_wb - 2*front_h*tan(side_taper);             // top width
lean     = front_h * tan(face_lean);
body_d   = max(wall + board_depth + wall, case_depth);
body_hb  = front_h * back_h_frac;
screen_cz = bezel_bottom + visible_h/2;
badge_cz  = bezel_bottom/2;

// side profile (depth Y, height Z): front leans back, hood slopes to a short back
function side_profile(d, hf, hb, ln) = [[0,0],[ln,hf],[d,hb],[d,0]];
// front outline (width X centred, height Z): gentle trapezoid
function front_outline(wb, wt, h) = [[-wb/2,0],[wb/2,0],[wt/2,h],[-wt/2,h]];

/* ===================== BODY (flat-panel intersection) ===================== */

module round2d(r) { offset(r = r) offset(delta = -r) children(); }

// thin wall (thickness t along X) whose YZ section is the side profile
module side_wall(t, prof) {
    rotate([90, 0, 90]) linear_extrude(t) round2d(edge_r) polygon(prof);
}

// prism following the side profile across a given width (flat top/front/back)
module profile_prism(width, prof) {
    hull() {
        translate([-width/2, 0, 0])      side_wall(0.5, prof);
        translate([ width/2 - 0.5, 0, 0]) side_wall(0.5, prof);
    }
}

// vertical mask giving the width taper + rounded vertical edges
module width_mask(wb, wt) {
    rotate([90, 0, 0]) linear_extrude(4*body_d, center = true)
        round2d(edge_r) polygon(front_outline(wb, wt, front_h));
}

module body_solid(dd, hf, hb, ln, wb, wt) {
    intersection() {
        profile_prism(wb, side_profile(dd, hf, hb, ln));
        width_mask(wb, wt);
    }
}

/* ===================== FEATURES ===================== */

module on_face() { rotate([-face_lean, 0, 0]) children(); }

module screen_hole(w, h, depth, rr) {
    hull() for (x = [-1, 1], z = [-1, 1])
        translate([x*(w/2 - rr), 0, z*(h/2 - rr)])
            rotate([-90, 0, 0]) cylinder(h = depth, r = rr);
}

module screen_cut() {
    translate([0, -eps, screen_cz]) screen_hole(visible_w, visible_h, 4*wall, 5);
    translate([0, -eps, screen_cz])
        screen_hole(visible_w + 2*recess_lip, visible_h + 2*recess_lip, recess_d + eps, 9);
}

// two full-width bars across the inner face (fuse wall-to-wall), holes drilled
module mount_bars() {
    bh = 12;
    for (z = [screen_cz - hole_dy/2, screen_cz + hole_dy/2]) {
        fw = front_wb - (front_wb - front_wt) * (z/front_h);
        difference() {
            translate([-(fw - 2*wall + 4)/2, 1.0, z - bh/2])
                cube([fw - 2*wall + 4, 14, bh]);
            for (x = [-hole_dx/2, hole_dx/2])
                translate([x, 1.0 - eps, z]) rotate([-90, 0, 0])
                    cylinder(h = 16, d = screw_d);
        }
    }
}

module badge_deboss() {
    if (badge != "")
        translate([0, 0.8, badge_cz]) rotate([90, 0, 0]) linear_extrude(1.4)
            text(badge, size = badge_size, halign = "center", valign = "center",
                 font = "Liberation Mono:style=Bold");
}

module rear_opening() {                         // access hole in the back wall
    ow = front_wt - 4*wall; oh = body_hb - 4*wall;
    translate([-ow/2, body_d - wall - eps, wall + 4]) cube([ow, wall + 2*eps, oh]);
}

module hood_vents() {
    n = 13; sw = 3.2; sl = front_wt*0.42;
    for (i = [0:n-1])
        translate([0, body_d*0.30 + i*5.2, 0])
            // slot lies along the hood; cut vertically through the top surface
            translate([-sl/2, 0, front_h]) cube([sl, sw, 40]);
}

module screen_fill() {
    color([0.05,0.06,0.05]) on_face()
        translate([0, wall - 0.8, screen_cz]) screen_hole(visible_w-1, visible_h-1, 1, 5);
}

/* ===================== ASSEMBLY ===================== */

module shell() {
    difference() {
        body_solid(body_d, front_h, body_hb, lean, front_wb, front_wt);
        translate([0, wall, wall])                          // inset so walls remain
            body_solid(body_d - 2*wall, front_h - 2*wall, body_hb - 2*wall,
                       lean * (front_h - 2*wall)/front_h,
                       front_wb - 2*wall, front_wt - 2*wall);
        on_face() screen_cut();
        on_face() badge_deboss();
        rear_opening();
    }
    on_face() mount_bars();
    if (show_screen) screen_fill();
}

module bezelcheck() {
    difference() {
        intersection() {
            profile_prism(front_wb, [[0,0],[lean,front_h],[lean+wall+8,front_h],[wall+8,0]]);
            width_mask(front_wb, front_wt);
        }
        on_face() screen_cut();
        on_face() badge_deboss();
    }
    on_face() mount_bars();
    if (show_screen) screen_fill();
}

if   (part == "bezelcheck") bezelcheck();
else                        shell();
