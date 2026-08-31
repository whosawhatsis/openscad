// Topology varies with $t: the sphere's facet count changes between frames, so the exported
// stage must time-sample faceVertexCounts/faceVertexIndices and not just points.
rotate([0, 0, $t * 90]) cube(10, center = true);
color("red") translate([20, 0, 0]) sphere(r = 5, $fn = 6 + floor($t * 4) * 2);
