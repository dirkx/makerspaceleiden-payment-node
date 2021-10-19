$fn=50;

D=3.3;
H=10;

module head(d=10) {
    union() {
        translate([0,0,d/10]) difference() {
        resize([d,d,d/2]) sphere(d/2);
        translate([-d,-d,-d*2]) cube([d*2,d*2,d*2]);
    };
    cylinder(h=d/10,r=d/2);
};
};

module bolt() {
translate([0,0,H]) head(D*1.3); cylinder(h=H,r=D/2);
};

module ring(d=10,h=10) {
    difference() {
        cylinder(h=h,r=d/2);
        translate([0,0,-0.1]) cylinder(h=h*1.1,r=D/2+0.05);
    };
};

for ( x = [0 : 1] ){
for ( y = [0 : 3] ){

translate([3*D*x,3*D*y]) {
    bolt();
    translate([6*D,0,0]) ring(D*1.5,3.6);
};
};
};