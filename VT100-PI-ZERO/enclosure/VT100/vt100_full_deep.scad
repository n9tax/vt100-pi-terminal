$fn=40;

//Header include modules here
//include <module.scad>;

//User Variables
scaleFactor = 4.3;

current_color = "ALL";
//current_color = "gray";
//current_color = "white";

//Derived Variables


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

module mainExtrusion(extrudeWidth) {
    module pp() {
        module p(){
            hull(){            
                linear_extrude(height = extrudeWidth) translate([0,0,0]) circle(d = 6); 
                linear_extrude(height = extrudeWidth) translate([60,0,0]) circle(d = 6); 
                linear_extrude(height = extrudeWidth) translate([57,52,0]) circle(d = 11); 
                linear_extrude(height = extrudeWidth) translate([0,60,0]) circle(d = 11); 
                linear_extrude(height = extrudeWidth) translate([-10,15.5,0]) circle(d = 6); 
            }
        }  

        module n(){
            hull() {
                linear_extrude(height = extrudeWidth) translate([-6, 9.5,0]) circle(d = 6); 
                linear_extrude(height = extrudeWidth) translate([-6, -3,0]) circle(d = 6); 
                linear_extrude(height = extrudeWidth) translate([-13, 9.5,0]) circle(d = 6); 
                linear_extrude(height = extrudeWidth) translate([-13, -3,0]) circle(d = 6); 
            }     

        }  
        rotate([90,0,90]) {
            difference() {
                p();
                n();
            }
        }
    }

    module nn() {
        translate([(56 / 2) + 6, 15, 45]) {
            rotate([-10,0,0]) {
                radiusCube(x = 56, y = 56, z = 56, r = 5.5);
            } 
        } 

        profile = [
            [0,   0   ],
            [7.3, 0   ],
            [5,   17.5],
            [7.3, 73  ],
            [0,   73  ]
        ];

        // 2D shape
        rotate([90,0,0]) translate([-5,-3,-80]) linear_extrude(height = 100) polygon(points = profile);
        mirror([180,0,0]) rotate([90,0,0]) translate([-95,-3,-80]) linear_extrude(height = 100) polygon(points = profile);

    }
    difference() {
        pp();
        nn();
    }
}

//Put things you want to build here.
module positive(){
  multicolor(color  = "gray"){
    scale(scaleFactor) mainExtrusion(extrudeWidth = 90);
  }

}

//Put cut outs here.
module negative(){
    multicolor(color  = "gray"){

        translate([1 * scaleFactor, .5 * scaleFactor, 1 * scaleFactor]) #scale(.97 * scaleFactor) mainExtrusion(extrudeWidth = 90);
      
    }

}

//Code

difference(){
  positive();
  negative();
}
