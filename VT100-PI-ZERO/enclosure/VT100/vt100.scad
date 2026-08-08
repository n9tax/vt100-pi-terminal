$fn=40;

//Header include modules here
//include <module.scad>;

// ---------------------------------------------------------------------------
// What to build.  "assembly" for the screen, the rest for exporting STLs.
// ---------------------------------------------------------------------------
// "assembly" | "case" (all four quadrants) | "whole" (the shell unsplit,
// for checking) | "lowerleft" | "lowerright" | "upperleft" | "upperright"
// | "bezel" | "panel" | "keyboard" | "kbleft" | "kbright"
part = "assembly";

// Parts are built where they live in the case, and the rear face leans back
// 0.513 deg, so the access door comes out as a flat plate that is not square to
// any axis.  Export it as-is, stand it up with a plain 90 deg rotation in the
// slicer, and one edge ends up 1.88mm off the bed.  Set this true when you go to
// print the door and it comes out lying on the bed, outer face down.
layFlat = false;

// Either selector on its own will isolate a part -- they do the same job.
// part = ...          picks a part by name
// current_color = ... picks it by the filament it prints in
//current_color = "ALL";     // everything
//current_color = "gray";    // shell, lower left
//current_color = "green";   // shell, lower right
//current_color = "blue";    // shell, upper left
//current_color = "red";     // shell, upper right
//current_color = "black";   // the screen bezel
//current_color = "white";   // the rear access door
//current_color = "orange";  // keyboard, left half
current_color = "yellow";  // keyboard, right half
// left/right are as you look at the front.  Set all four of quadColor at the
// bottom to "gray" if you want a plain preview instead of the harlequin.

//User Variables
scaleFactor = 4.3;

// Wall thickness, in FINAL mm, even all over.  3.4 is what the front wall
// happened to be before, so the bezel and its detent balls are unaffected.
wallThickness = 3.4;

// The screen aperture, in FINAL mm.  Note this is NOT an un scaled up
// dimension like the ones below it -- measure the panel and type the number
// straight in.  (It used to be [50, 36.5] un scaled up, i.e. 215 x 156.95.)
screenSize = [213, 160];       // viewable area of the LCD

// The circles the main profile is hulled from, un scaled up, as [depth, height].
// A face of the shape is the common tangent of two of these, so moving one moves
// a whole surface -- everything in "Derived" below is worked out from them.
//
// cRearTop used to be at 23, which put the back 0.51 deg off vertical: for a
// tangent to be vertical both circles have to reach the same depth, and
// 23 + 5.5 = 28.5 fell 0.5 short of 26 + 3 = 29.  At 23.5 the back is square to
// the base.  (Widening it to r = 6 at x = 23 would do the same job, but that
// changes the top rear corner radius, and 5.5 matches the front top corner.)
rSmall    = 3;
rBig      = 5.5;
cFrontBot = [  0,    0];
cRearBot  = [ 26,    0];
cRearTop  = [ 23.5, 56];
cFrontTop = [  0,   60];
cNose     = [-10,  15.5];

// un scaled up description of the opening in the front of the case.  The case
// cuts this out, the bezel is built to fill it back in, so both have to agree.
bezelSize     = [56, 25, 56];        // x = width, y = depth of the cutter, z = height
bezelPosition = [(56 / 2) + 6, 0, 45];
bezelRadius   = 5.5;                 // corner rounding of the opening
bezelTilt     = -10;                 // rake of the front face

// ---------------------------------------------------------------------------
// Everything below here is in FINAL mm, not un scaled up units -- screws,
// detents and vent slots are real world sizes and should not follow
// scaleFactor.  They are placed off the surface frames in "Derived" below.
// ---------------------------------------------------------------------------

// --- bezel snap fit -------------------------------------------------------
bezelFit       = 0.2;    // gap all round so the bezel can actually go in
bezelBallR     = 1.0;    // detent balls on the sides of the bezel
bezelBallProud = 0.7;    // how far they stand out of the bezel edge
bezelBallSlack = 0.15;   // dimples in the chassis are this much bigger
bezelBallPadR  = 4.0;    // material added behind each dimple so it cannot blow out
bezelBallZ     = [-85, -40, 5];   // heights up the opening, in the opening's own frame

// --- vent slits (top, behind the bezel) -----------------------------------
ventSpan   = [21.5, 262.3];  // x range they cover
ventCount  = 40;
ventWidth  = 3;
ventLength = 24;

// --- rear access panel ----------------------------------------------------
accessSize     = [86, 190];  // the hole in the back
accessCentre   = [312, 116]; // x, z of that hole on the rear face
accessRadius   = 8;          // corner rounding
panelLip       = 10;         // how far the panel overlaps the hole all round
panelThickness = 3.5;
panelFit       = 0.25;       // gap round the panel in its rebate
frameThickness = 7;          // material added inside for the screws to bite into
frameMargin    = 8;          // how far that frame reaches past the panel
screwInset     = 5;          // screw centres, in from the panel edge
screwPilot     = 2.6;        // M3 self tapper
screwClear     = 3.4;
screwHeadDia   = 6.0;
screwHeadDepth = 1.6;

// --- what goes in the access panel ----------------------------------------
// All positions are on the panel, measured from the middle of it.  The panel
// is panelW x panelH (106 x 210), so x is +/-53 and z is +/-105, and its own
// four screws sit at (+/-48, +/-100).  Everything here also has to stay inside
// the hole in the case (+/-43, +/-95) or it will foul the lip.
// Everything sits in one row along the bottom, to keep the weight of the
// cables as low as possible -- this case is light and a lead hanging off the
// top of the panel would want to tip it over.  Both connectors are stood on
// end (Rotate = 90) so all three fit across the 86mm of usable width.
powerHoleDia  = 11;            // grommet / cable gland for the mains lead
powerHolePos  = [26, -72];

de9Pos        = [-26, -72];
de9Rotate     = 90;            // 90 stands the connector on end
de9CutTop     = 19.2;          // D-sub DE9 panel cutout, wide side
de9CutBottom  = 16.9;          //                         narrow side
de9CutHeight  = 12.55;
de9CutCorner  = 1;
de9ScrewPitch = 24.99;         // jackscrew centres -- this one is a real standard
de9ScrewDia   = 3.2;

usbPos        = [0, -72];
usbRotate     = 90;
usbCut        = [14.5, 7.5];   // the opening itself -- MEASURE YOURS, no standard
usbCutCorner  = 1;
usbScrewPitch = 29.5;          // mounting holes, on the connector's long axis
usbScrewDia   = 3.5;

// --- keyboard ---------------------------------------------------------------
// Measured off the same drawing as the terminal, so these are un scaled up
// units like the profile circles at the top, NOT final mm.
//
// The front and rear corners are NOT the same radius.  The front is 5, and the
// front is 10 tall -- exactly one of those circles -- so both front corners land
// on the same centre and the nose is a true half round.  The rear corners are 3,
// the terminal's own bottom corner radius.
kbRadiusF = 5;             //       -> 21.50 radius, the two front corners
kbRadiusR = rSmall;        //  3    -> 12.90 radius, the two rear corners
kbFrontH  = 2 * kbRadiusF; // 10    -> 43.00 tall, one full circle
kbRearH   = 14.5;          //       -> 62.35 tall
kbDepth   = 39.5;          //       -> 169.85 front to back
kbWidth   = 90;            //       -> 387.00, same as the terminal

kbGap        = 40;         // FINAL mm, terminal's nose to the keyboard's back
kbOpenBottom = true;       // leave the underside open

// The recess the real keyboard drops into.  FINAL mm, not drawing units.
// kbPocketZ is how DEEP the cut is, measured straight down from the plane of the
// top face, at the pocket's front edge and at its rear edge.  (If those 22 / 30
// were meant as floor heights above the base instead, set kbPocketFromTop=false.)
kbPocket      = [363, 140];   // width x depth
kbPocketZ     = [22, 30];     // depth of the cut at the front / at the back
kbPocketFromTop = true;
kbFloor       = wallThickness; // the plate the keyboard sits on
kbCordW       = 5;             // slit down the back centre for the lead

// Halved for the printer.  Same glue strip idea as the terminal: a backing
// strip lapping the seam, tabFlange thick, with tabGlue of clearance.
kbSplit = kbWidth * scaleFactor / 2;

// --- splitting the shell into four -----------------------------------------
// The case is 387 x 294.55 overall (x 0..387, z -12.9..281.65), so these are
// the dead centre of each.  Move them if you would rather put a seam somewhere
// less awkward -- see the note about the access door in the comments below.
splitX = 193.5;
splitZ = 134.375;

// Glue tabs: a strip lying against the inside of the wall that laps across the
// seam into the next piece.  Nothing to fit or clip together, it is just a
// backing strip to spread the glue joint and line the halves up.
tabReach   = 12;      // how far the strip runs either side of the seam
tabFlange  = 2.5;     // how thick the strip is -- even everywhere now
tabGlue    = 0.15;    // glue gap between the lapping half and the next piece
tabClear   = 0.4;     // gap where the two seams cross, so the strips miss

// Both connectors get the panel thinned behind them, otherwise a D-sub shell
// or a USB coupler cannot reach through 3.5mm of plastic.
padLeft       = 2;             // thickness the panel is left at
de9PadSize    = [42, 27];      // long axis first -- the Rotate above turns it
usbPadSize    = [40, 14];      // what the USB housing needs to drop into
padCorner     = 3;

//Derived Variables

// The bezel opening, in mm, in its own tilted frame.
bezelHalfW = bezelSize[0] / 2 * scaleFactor;   // 120.4
bezelHalfH = bezelSize[2] / 2 * scaleFactor;   // 120.4

// --- surface planes, worked out from the hull circles ----------------------
// Where two circles of DIFFERENT radii meet a common tangent, the normal is
// not perpendicular to their centre line -- it is swung round by
// acos((r2-r1)/L).  Offsetting both centres along the same perpendicular (the
// obvious thing to do, and what I had here as hard coded numbers) is only right
// when the radii match, and puts these planes out by over a tenth of a mm.
// `out` is the direction the face looks in, and picks which tangent we want.
// The normal returned points INTO the solid, so adding wallThickness * n to a
// face slides it to where the inside of the wall is.
function tangentNormal(c1, r1, c2, r2, out) =
    let (v   = c2 - c1,
         L   = norm(v),
         u   = v / L,
         w   = [-u[1], u[0]],
         cf  = (r2 - r1) / L,
         sf2 = sqrt(1 - cf * cf),
         a   = u * cf + w * sf2,
         b   = u * cf - w * sf2)
    ((a * out) < (b * out)) ? a : b;

function tangentSeg(c1, r1, c2, r2, out) =
    let (n = tangentNormal(c1, r1, c2, r2, out))
    [c1 - r1 * n, c2 - r2 * n];       // where it touches each circle

rearSeg  = tangentSeg(cRearBot,  rSmall, cRearTop,  rBig, [ 1, 0]);
topSeg   = tangentSeg(cFrontTop, rBig,   cRearTop,  rBig, [ 0, 1]);
frontSeg = tangentSeg(cNose,     rSmall, cFrontTop, rBig, [-1, 0]);

// The rear face.  With cRearTop at 23.5 this comes out dead vertical, so
// rearLean is 0 and the access door is square to the base.
rearLo   = rearSeg[0] * scaleFactor;
rearHi   = rearSeg[1] * scaleFactor;
rearLean = atan2(rearLo[0] - rearHi[0], rearHi[1] - rearLo[1]);
rearRefY = rearLo[0] + rearLo[1] * tan(rearLean);

// The flat top, falling away toward the back.  The bezel's top wrap ends at
// y = 68.09, so the vent slits live behind that.
topFront = topSeg[0] * scaleFactor;
topRear  = topSeg[1] * scaleFactor;
topSlope = atan2(topRear[1] - topFront[1], topRear[0] - topFront[0]);
topRefY  = 86.5;
topRefZ  = topFront[1] + (topRefY - topFront[0]) * tan(topSlope);

// A world line Y = k + m*Z, restated in the bezel opening's tilted frame as
// [y at z = 0, dy/dz].  Used to find the front wall so the detent balls can sit
// in the middle of it.
function lineInBezel(P1, P2) =
    let (m   = (P2[0] - P1[0]) / (P2[1] - P1[1]),
         k   = P1[0] - m * P1[1],
         Cz  = bezelPosition[2] * scaleFactor,
         t   = bezelTilt,
         den = cos(t) - m * sin(t))
    [(k + m * Cz) / den, (m * cos(t) + sin(t)) / den];

// the front wall: the outer face, and the same face slid in by the wall
frontN     = tangentNormal(cNose, rSmall, cFrontTop, rBig, [-1, 0]);
frontOuter = lineInBezel(frontSeg[0] * scaleFactor, frontSeg[1] * scaleFactor);
frontInner = lineInBezel(frontSeg[0] * scaleFactor + frontN * wallThickness,
                         frontSeg[1] * scaleFactor + frontN * wallThickness);

function bezelPlateY(z) = ((frontOuter[0] + frontOuter[1] * z) +
                           (frontInner[0] + frontInner[1] * z)) / 2;

// where the terminal's nose and its base sit, so the keyboard can line up
terminalFrontY = (cNose[0]     - rSmall) * scaleFactor;   // -55.90
terminalBaseZ  = (cFrontBot[1] - rSmall) * scaleFactor;   // -12.90

// --- keyboard, worked out in its own frame: x 0..387, y 0..169.85 back from the
// nose, z 0..62.35 up from the base ----------------------------------------
kbTopSeg   = tangentSeg([kbRadiusF, kbRadiusF], kbRadiusF,
                        [kbDepth - kbRadiusR, kbRearH - kbRadiusR], kbRadiusR, [0, 1]);
kbTopF     = kbTopSeg[0] * scaleFactor;    // 18.47, 42.79  where the flat top starts
kbTopR     = kbTopSeg[1] * scaleFactor;    // 155.13, 62.22  and where it ends
kbTopSlope = atan2(kbTopR[1] - kbTopF[1], kbTopR[0] - kbTopF[0]);   // 8.09 deg
function kbTopAt(y) = kbTopF[1] + (y - kbTopF[0]) * tan(kbTopSlope);

// pocket centred in both directions
kbPocketX = (kbWidth * scaleFactor - kbPocket[0]) / 2;    // 12.00
kbPocketY = (kbDepth * scaleFactor - kbPocket[1]) / 2;    // 14.92
kbPocketY2 = kbPocketY + kbPocket[1];

// the floor the keyboard sits on, as a plane through the front and rear edges
kbFloorF = kbPocketFromTop ? kbTopAt(kbPocketY)  - kbPocketZ[0] : kbPocketZ[0];
kbFloorR = kbPocketFromTop ? kbTopAt(kbPocketY2) - kbPocketZ[1] : kbPocketZ[1];
function kbFloorAt(y) = kbFloorF + (y - kbPocketY) * (kbFloorR - kbFloorF) / kbPocket[1];

panelW = accessSize[0] + 2 * panelLip;
panelH = accessSize[1] + 2 * panelLip;
screwX = panelW / 2 - screwInset;
screwZ = panelH / 2 - screwInset;

//modules
/* Similar to the color function, but can be used for generating multi-color models for printing.
 * The global current_color variable indicates the color to print.
 */
module multicolor(color) {
    if (current_color != "ALL" && current_color != color) {
        // ignore our children.
        // (I originally used "scale([0,0,0])" which also works but isn't needed.)
    } else {
        color(color)
        children();
    }
}

module radiusCube(x, y, z, r) {
    hull()
        for (i = [-1, 1], j = [-1, 1], k = [-1, 1])
            translate([i*(x/2 - r), j*(y/2 - r), k*(z/2 - r)])
                sphere(r = r);
}

// the box that opens up the front of the case for the screen bezel.  Used as a
// cutter by the case and as the body the bezel is carved out of.  shrink pulls
// the whole thing in evenly, which is how the bezel gets its running fit.
module bezelCutout(shrink = 0) {
    translate(bezelPosition)
        rotate([bezelTilt, 0, 0])
            // taking shrink off every dimension and half of it off the corner
            // radius offsets the whole box inward by shrink/2 -- the depth
            // matters too, that face is what the bezel's top wrap seats against.
            radiusCube(x = bezelSize[0] - shrink,
                       y = bezelSize[1] - shrink,
                       z = bezelSize[2] - shrink,
                       r = bezelRadius - shrink / 2);
}

// module to cut out the hole for the bezel
// bezelCut = false builds the same shape with the screen opening left solid.
// The cross section of the case, un scaled up.  The whole shape is a prism
// along the width, so a genuinely uniform wall is just a 2D offset of this plus
// pulling the two ends in -- no scaled copy, no Minkowski.
module sectionRaw() {
    difference() {
        // the main shape formed from hulling circles.
        hull(){
            translate(cFrontBot) circle(r = rSmall);
            translate(cRearBot)  circle(r = rSmall);   // rear bottom circle
            translate(cRearTop)  circle(r = rBig);     // rear top circle
            // translate([60,0]) circle(d = 6);        // rear bottom circle old facroy depth
            // translate([57,52]) circle(d = 11);      // rear top circle old factory depth
            translate(cFrontTop) circle(r = rBig);
            translate(cNose)     circle(r = rSmall);
        }
        // undercut the front to form the chin
        hull() {
            translate([ -6,  9.5]) circle(d = 6);
            translate([ -6, -3  ]) circle(d = 6);
            translate([-13,  9.5]) circle(d = 6);
            translate([-13, -3  ]) circle(d = 6);
        }
    }
}

module section(inset = 0) {
    if (inset == 0) sectionRaw();
    else            offset(r = -inset) sectionRaw();
}

// the chamfer down each side, as a 2D shape.  grow > 0 swells the cutter, which
// is what leaves a uniform wall behind it rather than a wedge.
bevelProfile = [[0, 0], [7.3, 0], [5, 17.5], [7.3, 73], [0, 73]];
module bevel(grow = 0) {
    if (grow == 0) polygon(points = bevelProfile);
    else           offset(r = grow) polygon(points = bevelProfile);
}

// inset > 0 gives the shape eroded evenly by that much, in un scaled up units.
// Eroding a difference means eroding the body and DILATING what was cut out of
// it, which is why bevel() gets grown by the same amount.
module mainExtrusion(extrudeWidth, bezelCut = true, inset = 0) {
    module pp() {
        rotate([90,0,90])
            translate([0, 0, inset])
                linear_extrude(height = extrudeWidth - 2*inset)
                    section(inset);
    }

    module nn() {
        // cut out bezel hole
        if (bezelCut) bezelCutout();

        // cut out side bevels
        rotate([90,0,0]) translate([-5,-3,-80]) linear_extrude(height = 100) bevel(inset);
        mirror([180,0,0]) rotate([90,0,0]) translate([-95,-3,-80]) linear_extrude(height = 100) bevel(inset);
    }
    difference() {
        pp();
        nn();
    }
}

//Put things you want to build here.
module positive(bezelCut = true){
    // draw main shape and scale it to a factor that the screen will fit in
    scale(scaleFactor) mainExtrusion(extrudeWidth = 90, bezelCut = bezelCut);
}

//Put cut outs here.
// The shape eroded evenly by `mm`.  This used to be a 0.97 scaled copy shifted
// a bit, which cannot give an even wall however you place it -- the gap grows
// with distance from the origin, so the wall ran 1.6mm at the back and 7.3mm on
// the right cheek, a 4.6x spread, and the glue flanges thinned out in the same
// places.  A real offset is even everywhere by construction.
module shapeInset(mm) {
    scale(scaleFactor)
        mainExtrusion(extrudeWidth = 90, bezelCut = false, inset = mm / scaleFactor);
}

module negative(){
    // cut out the interior.
    //
    // The bezel opening is deliberately NOT cut from this inner copy.  The
    // opening is a through hole in the outer shape, so eroding that shape
    // already leaves a proper wall all round it, and cutting the opening here
    // as well used to leave a slab of plastic floating loose in the cavity.
    shapeInset(wallThickness);
}

// the hollow case wall.  bezelCut = false keeps the screen opening filled in,
// which is the stock the bezel gets cut from.
module shell(bezelCut = true) {
    difference() {
        positive(bezelCut = bezelCut);
        negative();
    }
}

// ---------------------------------------------------------------------------
// working frames, all in mm
// ---------------------------------------------------------------------------

// the screen opening's own frame: origin at the centre of the opening,
// x across, y out toward the viewer, z up the face.
module atBezel() {
    translate(bezelPosition * scaleFactor) rotate([bezelTilt, 0, 0]) children();
}

// the flat part of the top, behind the bezel: y runs back down the slope,
// z is the outward normal, the surface is at z = 0.
module atTop() {
    translate([0, topRefY, topRefZ]) rotate([topSlope, 0, 0]) children();
}

// the rear face: y points out the back, the face is at y = 0.
module atRear() {
    translate([0, rearRefY, 0]) rotate([rearLean, 0, 0]) children();
}

// a rounded plate lying on the rear face, extruded from the face inward.
module rearSlab(w, h, depth, r) {
    rotate([90, 0, 0])
        linear_extrude(height = depth)
            hull() for (i = [-1, 1], j = [-1, 1])
                translate([i*(w/2 - r), j*(h/2 - r)]) circle(r = r);
}

// the access panel's own frame: origin in the middle of it, y out the back,
// x across and z up the panel.
module atPanel() {
    atRear() translate([accessCentre[0], 0, accessCentre[1]]) children();
}

// The exact inverse of atPanel, then tipped over: this lays the door flat on
// the bed, centred on the origin, OUTER face down.  That way the connector
// pockets and the thinned sections open upward and need no support, and only
// the four small screw counterbores have to bridge.
module panelForPrinting() {
    translate([-accessCentre[0], -accessCentre[1], 0])
        rotate([-90, 0, 0])
            rotate([-rearLean, 0, 0])
                translate([0, -rearRefY, 0])
                    children();
}

// take a 2D profile drawn in x/z and push it in from the rear face.
module rearExtrude(depth) {
    rotate([90, 0, 0]) linear_extrude(height = depth) children();
}

// a hole bored straight through the panel from outside
module panelBore(pos, d, depth = 20) {
    atPanel() translate([pos[0], 2, pos[1]])
        rotate([90, 0, 0]) cylinder(h = depth, d = d, $fn = 32);
}

// ---------------------------------------------------------------------------
// vent slits
// ---------------------------------------------------------------------------
module ventSlits() {
    pitch = (ventSpan[1] - ventSpan[0]) / ventCount;
    for (i = [0 : ventCount - 1])
        atTop()
            translate([ventSpan[0] + pitch/2 + i*pitch, 0, -30])
                linear_extrude(height = 40)
                    hull() {
                        translate([0,  ventLength/2 - ventWidth/2]) circle(d = ventWidth, $fn = 16);
                        translate([0, -ventLength/2 + ventWidth/2]) circle(d = ventWidth, $fn = 16);
                    }
}

// ---------------------------------------------------------------------------
// bezel detents: balls on the sides of the bezel, dimples in the chassis
// ---------------------------------------------------------------------------

// centre of each ball, in the opening's frame.  Sitting this far in means
// bezelBallProud of it stands out past the (already shrunk) bezel edge.
function ballCentre(side, z) =
    [side * (bezelHalfW - bezelFit - (bezelBallR - bezelBallProud)), bezelPlateY(z), z];

module bezelBallPositions() {
    for (side = [-1, 1], z = bezelBallZ)
        atBezel() translate(ballCentre(side, z)) children();
}

// the balls, clipped to the front wall so they can never break its faces
module bezelBalls() {
    intersection() {
        bezelBallPositions() sphere(r = bezelBallR, $fn = 24);
        shell(bezelCut = false);
    }
}

module bezelDimples() {
    bezelBallPositions() sphere(r = bezelBallR + bezelBallSlack, $fn = 24);
}

// a little extra meat behind each dimple.  Clipping against positive() keeps it
// from bulging out of the case or intruding into the opening.
module bezelDetentPads() {
    intersection() {
        for (side = [-1, 1], z = bezelBallZ)
            atBezel() translate([side * (bezelHalfW + 1.5), bezelPlateY(z), z])
                sphere(r = bezelBallPadR, $fn = 24);
        positive();
    }
}

// ---------------------------------------------------------------------------
// rear access panel
// ---------------------------------------------------------------------------
module accessOpening() {
    // Only deep enough to break through the rear wall and the frame behind it.
    // Do NOT just make this arbitrarily deep -- the case is ~180mm front to
    // back, so an over-long cutter comes straight out the front of the cheek.
    atRear() translate([accessCentre[0], 20, accessCentre[1]])
        rearSlab(accessSize[0], accessSize[1],
                 20 + panelThickness + frameThickness + 10, accessRadius);
}

module accessRebate() {
    atRear() translate([accessCentre[0], 50, accessCentre[1]])
        rearSlab(panelW, panelH, 50 + panelThickness, accessRadius);
}

// the raw block the frame is cut from.  grow > 0 gives an oversized copy, used
// to keep the seam tabs out of the frame's way.
module accessFrameStock(grow = 0) {
    atRear() translate([accessCentre[0], grow, accessCentre[1]])
        rearSlab(panelW + 2*frameMargin + 2*grow, panelH + 2*frameMargin + 2*grow,
                 panelThickness + frameThickness + 2*grow, accessRadius + frameMargin + grow);
}

// local thickening on the inside of the rear wall for the screws to bite into
module accessFrame() {
    intersection() {
        accessFrameStock();
        positive();
    }
}

// --- what mounts in the access panel ---------------------------------------

// the pair of mounting holes either side of a connector, on its long axis
module connectorScrews(pos, ang, pitch, d) {
    for (i = [-1, 1])
        panelBore([pos[0] + i * pitch/2 * cos(ang),
                   pos[1] + i * pitch/2 * sin(ang)], d);
}

// D shaped, so the connector only goes in one way round.
module de9Cutout() {
    atPanel() translate([de9Pos[0], 2, de9Pos[1]])
        rearExtrude(20)
            rotate(de9Rotate)
                hull() for (i = [-1, 1]) {
                    translate([i*(de9CutTop/2    - de9CutCorner),
                                de9CutHeight/2 - de9CutCorner]) circle(r = de9CutCorner);
                    translate([i*(de9CutBottom/2 - de9CutCorner),
                               -de9CutHeight/2 + de9CutCorner]) circle(r = de9CutCorner);
                }
    connectorScrews(de9Pos, de9Rotate, de9ScrewPitch, de9ScrewDia);
}

module usbCutout() {
    atPanel() translate([usbPos[0], 2, usbPos[1]])
        rearExtrude(20)
            rotate(usbRotate)
                hull() for (i = [-1, 1], j = [-1, 1])
                    translate([i*(usbCut[0]/2 - usbCutCorner), j*(usbCut[1]/2 - usbCutCorner)])
                        circle(r = usbCutCorner);
    if (usbScrewPitch > 0)
        connectorScrews(usbPos, usbRotate, usbScrewPitch, usbScrewDia);
}

// pockets on the INSIDE of the panel, so the outside stays flush but the
// connectors only have padLeft of plastic to get their shells through.
module connectorPads() {
    for (p = [[de9Pos, de9PadSize, de9Rotate], [usbPos, usbPadSize, usbRotate]])
        atPanel() translate([p[0][0], -padLeft, p[0][1]])
            rearExtrude(20)
                rotate(p[2])
                    hull() for (i = [-1, 1], j = [-1, 1])
                        translate([i*(p[1][0]/2 - padCorner), j*(p[1][1]/2 - padCorner)])
                            circle(r = padCorner);
}

// four holes on the panel corners.  from/len are along the rear face normal.
module accessScrews(d, from, len) {
    for (i = [-1, 1], j = [-1, 1])
        atRear() translate([accessCentre[0] + i*screwX, from, accessCentre[1] + j*screwZ])
            rotate([90, 0, 0]) cylinder(h = len, d = d, $fn = 24);
}

// ---------------------------------------------------------------------------
// the parts
// ---------------------------------------------------------------------------
module vt100Case() {
    difference() {
        union() {
            shell(bezelCut = true);
            accessFrame();
            bezelDetentPads();
        }
        accessRebate();
        accessOpening();
        accessScrews(screwPilot, 1, panelThickness + frameThickness + 3);
        bezelDimples();
        ventSlits();
    }
}

// ---------------------------------------------------------------------------
// splitting the shell into four, with glue tabs
// ---------------------------------------------------------------------------

// The screen opening's outline swept straight back down its own axis.  Used to
// trim the glue strips instead of the rounded box itself: that box's corner
// rounding runs almost parallel to the front wall where it passes through, so it
// sliced the strips at a glancing angle and feathered them away to nothing.  A
// prism cuts them square, at 0.5 deg off the wall normal, so the strip keeps its
// full thickness right up to its edge.
// It has to STOP at the back of the opening.  Run it the whole depth of the
// case and it carries on through the rear wall and deletes the strip up the
// back, which is nothing to do with the screen opening.
module bezelPrism(grow = 0) {
    back = bezelSize[1] / 2 * scaleFactor + grow;   // the opening's own rear face
    translate(bezelPosition * scaleFactor)
        rotate([bezelTilt, 0, 0])
            rotate([90, 0, 0])
                translate([0, 0, -back])
                    linear_extrude(height = 400 + back)
                        offset(r = grow)
                            hull() for (i = [-1, 1], j = [-1, 1])
                                translate([i * (bezelHalfW - bezelRadius * scaleFactor),
                                           j * (bezelHalfH - bezelRadius * scaleFactor)])
                                    circle(r = bezelRadius * scaleFactor);
}

// big half spaces.  the model lives inside x 0..387, y -56..125, z -13..282.
module slabX(a, b) { translate([a, -600, -600]) cube([b - a, 1400, 1400]); }
module slabZ(a, b) { translate([-600, -600, a]) cube([1400, 1400, b - a]); }

// a layer lying against the inside of the cavity wall.  Because the hollowing
// is done by scaling, this comes out a little thicker far from the origin than
// near it -- for a glue tab that does not matter.
module wallLayer(fromWall) {
    difference() {
        shapeInset(fromWall);
        shapeInset(wallThickness + tabFlange);
    }
}

// ...trimmed out of every opening, so no tab ever floats across the screen
// hole or the access hole, blocks a vent, or fights with the access frame.
module tabStock(fromWall) {
    difference() {
        wallLayer(fromWall);
        bezelPrism(tabClear);
        accessOpening();
        accessFrameStock(tabClear);
        ventSlits();
    }
}

// A seam strip is flush with the wall on its own side, so it welds onto its
// own piece, and inset by the glue gap where it laps into the neighbour.
module seamStrip(axis) {
    union() {
        intersection() {
            tabStock(wallThickness);
            if (axis == "x") slabX(splitX - tabReach, splitX);
            else             slabZ(splitZ - tabReach, splitZ);
        }
        intersection() {
            tabStock(wallThickness + tabGlue);
            if (axis == "x") slabX(splitX, splitX + tabReach);
            else             slabZ(splitZ, splitZ + tabReach);
        }
    }
}

// the vertical seam's strip belongs to the -x side and laps into +x
module xSeamTab() { seamStrip("x"); }

// the horizontal seam's strip belongs to the -z side and laps into +z, notched
// where the vertical strip crosses it so the two never claim the same space
module zSeamTab() {
    difference() {
        seamStrip("z");
        slabX(splitX - tabReach - tabClear, splitX + tabReach + tabClear);
    }
}

// sx, sz say which side of each seam this quadrant is on.  A quadrant carries
// the strip for a seam only when it is on the -ve side of it, so the lower left
// piece carries both, the upper right carries neither.
module quadrant(sx, sz) {
    xlo = (sx < 0) ? -600 : splitX;   xhi = (sx < 0) ? splitX : 800;
    zlo = (sz < 0) ? -600 : splitZ;   zhi = (sz < 0) ? splitZ : 800;
    union() {
        intersection() {
            vt100Case();
            slabX(xlo, xhi);
            slabZ(zlo, zhi);
        }
        if (sx < 0) intersection() { xSeamTab(); slabZ(zlo, zhi); }
        if (sz < 0) intersection() { zSeamTab(); slabX(xlo, xhi); }
    }
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------

// the side profile, in the same [depth, height] frame the terminal section uses
module kbSectionRaw() {
    hull() {
        translate([kbRadiusF,           kbRadiusF            ]) circle(r = kbRadiusF);
        translate([kbRadiusF,           kbFrontH - kbRadiusF ]) circle(r = kbRadiusF);
        translate([kbDepth - kbRadiusR, kbRadiusR            ]) circle(r = kbRadiusR);
        translate([kbDepth - kbRadiusR, kbRearH  - kbRadiusR ]) circle(r = kbRadiusR);
    }
}

module kbSection(inset = 0) {
    if (inset == 0) kbSectionRaw();
    else            offset(r = -inset) kbSectionRaw();
}

// extruded across the width, same trick and same even wall as the terminal
module kbBody(inset = 0) {
    rotate([90, 0, 90])
        translate([0, 0, inset])
            linear_extrude(height = kbWidth - 2 * inset)
                kbSection(inset);
}

// the shape eroded evenly by `mm`, in the keyboard's own final-mm frame
module kbSolid(mm) { scale(scaleFactor) kbBody(mm / scaleFactor); }

// puts the keyboard's own frame where it belongs, in front of the terminal
module atKeyboard() {
    translate([0, terminalFrontY - kbGap - kbDepth * scaleFactor, terminalBaseZ])
        children();
}

// a profile drawn in [depth, height] and swept across the width
module kbSweep(x0, len) {
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(height = len) children();
}

// the recess, from the floor plane straight up and out through the top
module kbPocketCut() {
    kbSweep(kbPocketX, kbPocket[0])
        polygon([[kbPocketY,  kbFloorAt(kbPocketY) ],
                 [kbPocketY2, kbFloorAt(kbPocketY2)],
                 [kbPocketY2, 300],
                 [kbPocketY,  300]]);
}

// the plate the keyboard rests on, kbFloor thick, carried right across inside
module kbFloorPlate() {
    intersection() {
        kbSolid(wallThickness);
        kbSweep(0, kbWidth * scaleFactor)
            polygon([[-100, kbFloorAt(-100) - kbFloor],
                     [ 400, kbFloorAt( 400) - kbFloor],
                     [ 400, kbFloorAt( 400)],
                     [-100, kbFloorAt(-100)]]);
    }
}

// Slit down the back centre for the lead.  It stops at the underside of the
// board, i.e. the pocket floor -- run it to the base of the case instead and it
// saws through the floor plate and the glue strip below it, leaving both hanging
// either side of a slot that serves no purpose.
module kbCordCut() {
    z0 = kbFloorAt(kbPocketY2);
    translate([(kbWidth * scaleFactor - kbCordW)/2, kbPocketY2, z0])
        cube([kbCordW,
              kbDepth * scaleFactor - kbPocketY2 + 10,
              kbRearH * scaleFactor + 20 - z0]);
}

module keyboardShell() {
    w = wallThickness;
    difference() {
        union() {
            difference() {
                kbSolid(0);
                kbSolid(w);
                // Open the underside, over the flat part of the base only --
                // carried out to the nose the front wall thins to a knife edge
                // where the outer radius curves away.
                if (kbOpenBottom)
                    translate([w, kbRadiusF * scaleFactor + w, -1])
                        cube([kbWidth * scaleFactor - 2*w,
                              (kbDepth - kbRadiusF - kbRadiusR) * scaleFactor - 2*w,
                              1 + w]);
            }
            kbFloorPlate();
        }
        kbPocketCut();
        kbCordCut();
    }
}

// a slab lying under the floor plate, so the plate gets a lapped glue joint too
// rather than a bare butt down the middle of the tray the keyboard sits on
module kbUnderPlate(gap) {
    intersection() {
        kbSolid(wallThickness);
        kbSweep(0, kbWidth * scaleFactor)
            polygon([[-100, kbFloorAt(-100) - kbFloor - tabFlange],
                     [ 400, kbFloorAt( 400) - kbFloor - tabFlange],
                     [ 400, kbFloorAt( 400) - kbFloor - gap],
                     [-100, kbFloorAt(-100) - kbFloor - gap]]);
    }
}

// Glue strip lapping the seam, same idea as the terminal: flush with the wall on
// its own side so it welds on, dropped by `gap` where it laps into the other
// half.  The gap has to apply to the floor plate as well as the walls -- take it
// off only the walls and the strip drives straight through the other half's
// floor.
// The slit sits dead centre on the seam, so it cuts the glue strip in two and
// the half beyond it hangs off nothing but a wrap under the slit -- a free fin
// standing next to the opening.  Keep the strip out of the slit's region
// altogether; it carries on below the slit and right round the rest of the loop.
// It starts tabClear ahead of the pocket's rear face and tabClear below the
// pocket floor, so none of its faces land on a plane a different cutter already
// uses -- coincident cutter faces there leave non-manifold edges in the export.
// The overlap into the pocket costs nothing, the strip is already gone in there.
module kbCordKeepOut() {
    z0 = kbFloorAt(kbPocketY2) - tabClear;
    translate([kbSplit - tabReach - tabClear, kbPocketY2 - tabClear, z0])
        cube([2 * (tabReach + tabClear),
              kbDepth * scaleFactor - kbPocketY2 + 10,
              kbRearH * scaleFactor + 20 - z0]);
}

// the band the floor plate occupies, grown by `grow`
module kbPlateBand(grow) {
    kbSweep(0, kbWidth * scaleFactor)
        polygon([[-100, kbFloorAt(-100) - kbFloor - grow],
                 [ 400, kbFloorAt( 400) - kbFloor - grow],
                 [ 400, kbFloorAt( 400) + grow],
                 [-100, kbFloorAt(-100) + grow]]);
}

module kbSeamStrip(gap, dodgePlate) {
    difference() {
        union() {
            difference() {
                kbSolid(wallThickness + gap);
                kbSolid(wallThickness + tabFlange);
                // On the half that laps into its neighbour the wall layer has to
                // stand clear of THAT half's floor plate, or it drives straight
                // through it.  On its own side it just merges, so leave it.
                if (dodgePlate) kbPlateBand(gap);
            }
            kbUnderPlate(gap);
        }
        kbPocketCut();
        kbCordKeepOut();   // a superset of kbCordCut, across the whole strip
    }
}

module kbSeamTab() {
    union() {
        intersection() { kbSeamStrip(0,       false); slabX(kbSplit - tabReach, kbSplit); }
        intersection() { kbSeamStrip(tabGlue, true);  slabX(kbSplit, kbSplit + tabReach); }
    }
}

// side < 0 is the left half, and carries the strip
module keyboardHalf(side) {
    atKeyboard() union() {
        intersection() {
            keyboardShell();
            slabX(side < 0 ? -600 : kbSplit, side < 0 ? kbSplit : 800);
        }
        if (side < 0) kbSeamTab();
    }
}

module accessPanel() {
    difference() {
        intersection() {
            atRear() translate([accessCentre[0], 50, accessCentre[1]])
                rearSlab(panelW - 2*panelFit, panelH - 2*panelFit,
                         50 + panelThickness, accessRadius - panelFit);
            union() {
                shell(bezelCut = true);
                accessFrame();
            }
        }
        accessScrews(screwClear, 1, panelThickness + 2);
        accessScrews(screwHeadDia, 1, 1 + screwHeadDepth);
        connectorPads();
        de9Cutout();
        usbCutout();
        panelBore(powerHolePos, powerHoleDia);
    }
}

// the screen aperture, in the bezel's own frame: centred left to right, and
// lifted off the bottom of the opening by the same margin it has at the sides.
module screenHole() {
    sideMargin = (2 * bezelHalfW - screenSize[0]) / 2;
    zOffset    = -bezelHalfH + sideMargin + screenSize[1] / 2;

    atBezel() translate([0, 0, zOffset])
        cube([screenSize[0], bezelSize[1] * scaleFactor * 3, screenSize[1]], center = true);
}

// the black bezel: the plug that fills the opening the case cuts out, pulled in
// by bezelFit so it will go in, with the screen aperture taken back out of it.
module bezel() {
    difference() {
        union() {
            intersection() {
                scale(scaleFactor) bezelCutout(shrink = 2 * bezelFit / scaleFactor);
                shell(bezelCut = false);
            }
            bezelBalls();
        }
        screenHole();
    }
}

//Code

// the four pieces of the shell: name, colour, which side of each seam
quadName  = ["lowerleft", "lowerright", "upperleft", "upperright"];
quadColor = ["gray",      "green",      "blue",      "red"];
quadSide  = [[-1,-1],     [1,-1],       [-1,1],      [1,1]];

for (q = [0 : 3])
    if (part == "assembly" || part == "case" || part == quadName[q])
        multicolor(color = quadColor[q]) quadrant(quadSide[q][0], quadSide[q][1]);

if (part == "whole")                       multicolor(color = "gray")  vt100Case();

// the keyboard, halved down the middle for the printer
kbName  = ["kbleft",  "kbright"];
kbColor = ["orange",  "yellow"];
for (k = [0 : 1])
    if (part == "assembly" || part == "keyboard" || part == kbName[k])
        multicolor(color = kbColor[k]) keyboardHalf(k == 0 ? -1 : 1);

if (part == "assembly" || part == "bezel") multicolor(color = "black") bezel();
if (part == "assembly" || part == "panel") multicolor(color = "white") {
    if (layFlat) panelForPrinting() accessPanel();
    else                            accessPanel();
}
