"""End-to-end Canny contract, including mesh-producing OpenCSG products."""
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image


def main():
    executable, backend = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="openscad-canny-") as directory:
        root = pathlib.Path(directory)

        def render(source, width="1", final=False, success=True):
            scene = root / "scene.scad"
            output = root / "map.png"
            scene.write_text(source)
            command = [executable, str(scene), "-o", str(output),
                       "--export-format=cannymap", "--edge-width=" + width,
                       "--backend=" + backend, "--imgsize=128,128",
                       "--camera=0,0,80,0,0,0", "--projection=o"]
            if final:
                command.append("--render")
            result = subprocess.run(command, capture_output=True, text=True)
            if not success:
                assert result.returncode != 0, "invalid width accepted"
                return
            assert result.returncode == 0, (source, final, result.returncode, result.stdout + result.stderr)
            with Image.open(output) as image:
                assert image.mode == "L", image.mode
                pixels = list(image.tobytes())
                assert set(pixels) <= {0, 255}, "nonbinary edge samples"
                return pixels

        cube = "cube(20, center=true);"
        baseline = render(cube)
        assert 20 < sum(p == 255 for p in baseline) < 1000, "missing/filled silhouette"
        assert baseline[64 * 128 + 64] == 0, "flat face is not an edge"
        assert not any(render(cube, "0")), "zero width must be empty"
        thin = render(cube, "0.5")
        thick = render(cube, "3")
        assert sum(thin) <= sum(baseline) < sum(thick), "width does not control coverage"
        assert all(render(cube, "1e100")), "large finite width was rejected or wrapped"
        for width in ["-1", "nan", "inf"]:
            render(cube, width, success=False)
        for final in [False, True]:
            for source in [cube, "render() " + cube,
                           "minkowski() {cube(16,center=true); sphere(2,$fn=12);}",
                           "difference(){cube(20,center=true); cylinder(h=30,r=4,center=true,$fn=24);}"]:
                assert any(render(source, final=final)), (source, final)
        # Adjacent coplanar colors must create an internal edge without lighting.
        colored = render('color("red") translate([-10,-10,-10]) cube([10,20,20]);'
                         'color("blue") translate([0,-10,-10]) cube([10,20,20]);')
        assert any(colored[y * 128 + x] for y in range(55, 73) for x in range(63, 66)), \
            "raw-color boundary missing"
        # The threshold includes equality in the transparent layer.
        rear = 'translate([0,0,-8]) cube(8,center=true);'
        transparent = render('color([1,0,0,0.5]) cube(20,center=true);' + rear)
        opaque = render('color([1,0,0,0.51]) cube(20,center=true);' + rear)
        assert sum(transparent) > sum(opaque), "transparent surface occludes rear edges"
        assert render('color([1,0,0,0]) cube(20,center=true);' + rear) == transparent, \
            "zero alpha suppressed the transparent object's own edges"
        same_color = render('color("red") translate([-10,-10,-10]) cube([10,20,20]);'
                            'color("red") translate([0,-10,-10]) cube([10,20,20]);')
        assert not any(same_color[y * 128 + x] for y in range(55, 73) for x in range(63, 66)), \
            "coplanar same-color seam"
        sphere = render('sphere(10,$fn=96);')
        assert not any(sphere[y * 128 + x] for y in range(58,70) for x in range(58,70)), \
            "smooth sphere facets generated edges"
        assert any(render('square(20,center=true);', final=True)), "pure 2D contour missing"


if __name__ == "__main__":
    main()
