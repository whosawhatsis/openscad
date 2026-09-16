#!/usr/bin/env python3
"""The worker end of process isolation: `openscad --compute-worker`.

The GUI spawns this and hands it one end of an already-connected channel, identified by an
environment variable rather than a command-line argument so it is not visible to anything listing
processes. What is checked here is the wiring at the process boundary -- that the binary recognises
the mode, adopts the channel, and above all *exits* when the channel finishes. A worker that
lingers after its window has gone would leak one process per window per session.

POSIX only: handing a descriptor to a child needs `pass_fds`, and the Windows equivalent passes an
inherited HANDLE, which Python's subprocess does not expose. The Windows side of the same wiring is
covered by the ComputeWorker unit tests, which spawn a real child on every platform.
"""

import hashlib
import json
import os
import shutil
import tempfile
import socket
import subprocess
import sys
import unittest

from ipc_channel import read_message, request, write_message

CHANNEL_VARIABLE = "OPENSCAD_IPC_CHANNEL"
TIMEOUT = 30
# A worker that is going to answer answers promptly; waiting the full process timeout for a reply
# that is never coming just makes a red run slow.
REPLY_TIMEOUT = 10


def openscad_binary():
    """Deliberately fails rather than skipping. A skip counts as a pass, so a missing binary would
    let this whole file quietly verify nothing -- which is indistinguishable from working."""
    path = os.environ.get("OPENSCAD_BINARY")
    if not path:
        raise AssertionError("OPENSCAD_BINARY is not set; ctest sets it from OPENSCAD_BINPATH")
    if not os.path.exists(path):
        raise AssertionError(f"OPENSCAD_BINARY does not exist: {path}")
    return path


class WorkerFixture:
    """Spawning helper shared by the test cases below. Deliberately not a TestCase: inheriting from
    one would re-run its tests in every subclass."""

    def read_until_done(self, parent):
        """Collects payloads until the request is answered. A reply is no longer the first thing on
        the channel: the geometry arrives first, and it is binary."""
        payloads = {}
        while True:
            message = read_message(parent)
            if message is None:
                raise AssertionError("the worker closed the channel before answering")
            name, body = message
            if name == "done":
                return payloads, json.loads(body)
            payloads[name] = body

    def write_scad(self, text):
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        path = os.path.join(directory, "model.scad")
        with open(path, "w") as handle:
            handle.write(text)
        return path

    def start_worker(self, channel_value=None, with_channel=True):
        """Starts a worker. Returns (process, parent_socket); parent_socket is None when the
        worker was deliberately given no usable channel."""
        parent = None
        env = dict(os.environ)
        pass_fds = ()

        if with_channel:
            parent, child = socket.socketpair()
            env[CHANNEL_VARIABLE] = str(child.fileno())
            pass_fds = (child.fileno(),)
        elif channel_value is None:
            env.pop(CHANNEL_VARIABLE, None)
        else:
            env[CHANNEL_VARIABLE] = channel_value

        process = subprocess.Popen(
            [openscad_binary(), "--compute-worker"],
            env=env,
            pass_fds=pass_fds,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if with_channel:
            # The child owns it now. Holding a copy here would keep the channel open and the
            # worker would never see it finish.
            child.close()

        self.addCleanup(self.reap, process, parent)
        return process, parent

    @staticmethod
    def reap(process, parent):
        if parent is not None:
            parent.close()
        if process.poll() is None:
            process.kill()
        process.wait(timeout=TIMEOUT)


@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerEntryPoint(WorkerFixture, unittest.TestCase):
    def test_worker_exits_when_the_channel_closes(self):
        """The window has gone. The worker must go too, rather than linger as an orphan."""
        process, parent = self.start_worker()
        parent.close()
        self.assertEqual(process.wait(timeout=TIMEOUT), 0)

    def test_worker_refuses_a_missing_channel(self):
        """Started without a channel it must fail, not sit there doing nothing."""
        process, _ = self.start_worker(with_channel=False)
        self.assertNotEqual(process.wait(timeout=TIMEOUT), 0)

    def test_worker_refuses_a_channel_that_names_nothing(self):
        process, _ = self.start_worker(channel_value="not-a-descriptor", with_channel=False)
        self.assertNotEqual(process.wait(timeout=TIMEOUT), 0)

    def test_worker_mode_is_recognised_before_argument_parsing(self):
        """--compute-worker has to be handled before the usual option parsing and any GUI setup, so
        that it works in a headless build and never puts a window on screen."""
        process, parent = self.start_worker()
        parent.close()
        process.wait(timeout=TIMEOUT)
        self.assertNotIn(b"Usage", process.stderr.read())



@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerRequests(WorkerFixture, unittest.TestCase):
    """The request protocol itself.

    Control travels over the same framed channel as payloads rather than over stdin/stdout: a
    request is a message named "request" carrying JSON, and the worker answers with one named
    "done". Nothing here evaluates geometry yet -- what is being pinned down is that the worker
    understands a request at all, survives a bad one, and stays available for the next.
    """

    def exchange(self, **fields):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        fields.setdefault("input", self.write_scad("cube(1);"))
        fields.setdefault("output", "result.osig")
        request(parent, **fields)
        _, body = self.read_until_done(parent)
        return process, parent, ("done", body)

    def test_worker_answers_a_request(self):
        _, _, (name, body) = self.exchange(command="render", requestId=7)
        self.assertEqual(name, "done")
        self.assertEqual(body.get("requestId"), 7)
        self.assertTrue(body.get("ok"), f"expected success, got {body}")

    def test_worker_reports_an_unknown_command_without_dying(self):
        """A request it does not understand is an error to report, not a reason to take the
        window's worker down."""
        process, parent, (name, body) = self.exchange(command="not-a-command", requestId=1)
        self.assertEqual(name, "done")
        self.assertFalse(body.get("ok"))
        self.assertIn("error", body)
        self.assertIsNone(process.poll(), "the worker exited instead of reporting the error")

    def test_worker_survives_malformed_json(self):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        write_message(parent, "request", "{ this is not json")
        _, body = self.read_until_done(parent)
        name = "done"
        self.assertEqual(name, "done")
        self.assertFalse(body.get("ok"))
        self.assertIsNone(process.poll())

    def test_worker_ignores_a_message_it_does_not_know(self):
        """Forward compatibility: a name from a newer parent must not wedge an older worker."""
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        write_message(parent, "something-from-the-future", b"\x00\x01")
        request(parent, command="render", requestId=2,
                input=self.write_scad("cube(1);"), output="r.osig")
        _, body = self.read_until_done(parent)
        name = "done"
        self.assertEqual(name, "done")
        self.assertEqual(body.get("requestId"), 2)

    def test_worker_serves_several_requests_in_order(self):
        """The worker is persistent -- that is what makes a repeat render cheap. Each request is
        answered, in order, over one connection."""
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        for identifier in range(4):
            request(parent, command="render", requestId=identifier,
                    input=self.write_scad("cube(1);"), output=f"r{identifier}.osig")
            _, body = self.read_until_done(parent)
            name = "done"
            self.assertEqual(name, "done")
            self.assertEqual(body.get("requestId"), identifier)
        self.assertIsNone(process.poll())



@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerGeometry(WorkerFixture, unittest.TestCase):
    """Evaluating a request and returning geometry over the channel.

    The worker returns the geometry as a payload named after the file it would otherwise have
    written, so the parent can resolve references to it by name instead of touching the filesystem.
    The bytes are the internal binary format from io/ipc_geometry.h, which begins with the magic
    "OSIG"; this checks the framing and the plumbing, while the codec has its own unit tests.
    """

    def render(self, source, output="result.osig", **extra):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="render", requestId=1, input=self.write_scad(source),
                output=output, **extra)
        payloads, done = self.read_until_done(parent)
        return process, payloads, done

    def test_render_returns_geometry_over_the_channel(self):
        process, payloads, done = self.render("cube([10, 10, 10]);")
        self.assertTrue(done.get("ok"), f"render failed: {done}")
        self.assertIn("result.osig", payloads,
                      f"no geometry payload; got {sorted(payloads)}")
        self.assertTrue(payloads["result.osig"].startswith(b"OSIG"),
                        "payload is not the internal geometry format")
        self.assertGreater(len(payloads["result.osig"]), 64, "payload is too small to be a cube")

    def test_render_writes_no_file_for_its_geometry(self):
        """The payload replaces the file, it does not accompany it. A preview that still wrote one
        file per leaf would put the disk back in the path this feature exists to take it out of."""
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        output = os.path.join(directory, "result.osig")
        process, payloads, done = self.render("cube([10, 10, 10]);", output=output)
        self.assertTrue(done.get("ok"), f"render failed: {done}")
        self.assertFalse(os.path.exists(output), "the worker wrote the geometry to disk as well")

    def test_a_model_that_fails_to_parse_is_reported(self):
        process, payloads, done = self.render("this is not valid openscad ((((")
        self.assertFalse(done.get("ok"), "a broken model was reported as a successful render")
        self.assertIn("error", done)
        self.assertIsNone(process.poll(), "the worker exited instead of reporting the failure")

    def test_the_worker_survives_a_failed_render_and_serves_the_next(self):
        """One bad model must not cost the window its worker, or its warm caches."""
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="render", requestId=1,
                input=self.write_scad("nonsense ((("), output="bad.osig")
        _, first = self.read_until_done(parent)
        self.assertFalse(first.get("ok"))
        request(parent, command="render", requestId=2,
                input=self.write_scad("sphere(5);"), output="good.osig")
        payloads, done = self.read_until_done(parent)
        self.assertTrue(done.get("ok"), f"the second render failed: {done}")
        self.assertEqual(done.get("requestId"), 2)
        self.assertIn("good.osig", payloads)



@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerRenderVariables(WorkerFixture, unittest.TestCase):
    """The `$` variables a window owns have to travel with every request.

    `MainWindow::setRenderVariables()` fills `$preview`, `$t`, `$vpr`, `$vpt`, `$vpd` and `$vpf` from
    the window's animation time and its viewport camera. The worker is a separate process and can see
    neither, so unless the request carries them the model is evaluated with `$t = 0` and a
    default-constructed camera -- animation produces the same frame every time, and a model that
    orients itself to the viewport renders differently than it does in-process.

    Each is checked by making the geometry depend on it and requiring the payload to change, which
    holds whatever the encoding does. The camera can be moved between two renders, so these travel
    per request rather than being set once when the worker starts.
    """

    def render(self, source, **extra):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="render", requestId=1, input=self.write_scad(source),
                output="result.osig", **extra)
        payloads, done = self.read_until_done(parent)
        self.assertTrue(done.get("ok"), f"render failed: {done}")
        self.assertIn("result.osig", payloads)
        # A digest, not the bytes: a failure here should say which variable did not arrive, not
        # print two meshes.
        return hashlib.sha256(payloads["result.osig"]).hexdigest()

    def test_animation_time_reaches_the_model(self):
        """Without this every frame of an animation is identical."""
        model = "cube([1 + $t * 10, 1, 1]);"
        still = self.render(model)
        moved = self.render(model, time=0.5)
        self.assertNotEqual(still, moved,
                            "$t did not reach the model: the same geometry came back for t=0 and "
                            "t=0.5, so an isolated animation renders one frame over and over")

    def test_the_viewport_variables_reach_the_model(self):
        for variable, source, first, second in (
            ("$vpd", "cube([$vpd / 100, 1, 1]);", {"vpd": 140.0}, {"vpd": 500.0}),
            ("$vpf", "cube([$vpf / 10, 1, 1]);", {"vpf": 22.5}, {"vpf": 45.0}),
            ("$vpr", "cube([1 + $vpr[0] / 10, 1, 1]);",
             {"vpr": [0.0, 0.0, 0.0]}, {"vpr": [90.0, 0.0, 0.0]}),
            ("$vpt", "cube([1 + $vpt[0] / 10, 1, 1]);",
             {"vpt": [0.0, 0.0, 0.0]}, {"vpt": [50.0, 0.0, 0.0]}),
        ):
            with self.subTest(variable=variable):
                self.assertNotEqual(
                    self.render(source, **first), self.render(source, **second),
                    f"{variable} did not reach the model; a model that orients itself to the "
                    "viewport renders differently under isolation than in-process")


@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorker2DRender(WorkerFixture, unittest.TestCase):
    """An isolated F6 must accept a 2D top-level object, exactly as the in-process path does.

    `FileFormat::IPC_GEOMETRY` -- the worker's transport for a render result -- is classified 3D by
    `fileformat::is3D()`, so `do_export()` would derive `dim = 3` and `checkAndExport()` would reject
    any 2D top level with "Current top level object is not a 3D object". The worker then answers with
    an error and the window keeps whatever geometry it was already showing, which reads to the user as
    a stale or wrong render rather than as a failure.

    The transport has always carried `Polygon2d` (see `appendBody` in io/ipc_geometry.cc); only the
    dimension gate was wrong. The 3D case is asserted alongside so that simply deleting the gate does
    not pass this.
    """

    def render(self, source):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="render", requestId=1, input=self.write_scad(source),
                output="result.osig")
        payloads, done = self.read_until_done(parent)
        return payloads, done

    def assertRendered(self, source, description):
        payloads, done = self.render(source)
        self.assertTrue(done.get("ok"),
                        f"the isolated render of {description} failed: {done}; the window gets no "
                        "geometry and keeps showing what it had")
        self.assertIn("result.osig", payloads,
                      f"{description} answered ok but sent no geometry payload")
        self.assertTrue(payloads["result.osig"].startswith(b"OSIG"))

    def test_a_2d_top_level_object_renders(self):
        self.assertRendered("difference() { square(20, center = true); circle(5); }",
                            "a 2D top level")

    def test_a_2d_top_level_reached_through_a_root_modifier_renders(self):
        """How this was originally reported: the root modifier selects a 2D subtree out of a model
        whose top level is otherwise 3D."""
        self.assertRendered(
            "translate([50, 0, 0]) cube(5);\n"
            "linear_extrude(10) !difference() { square(20, center = true); circle(5); }",
            "a 2D subtree selected by a root modifier")

    def test_a_3d_top_level_object_still_renders(self):
        self.assertRendered("difference() { cube(10, center = true); sphere(6.4, $fn = 24); }",
                            "a 3D top level")


@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerPreview(WorkerFixture, unittest.TestCase):
    """A preview returns a CSG product list, not a single mesh.

    The parent composites the preview itself, so what it needs is the structure -- which leaves,
    where, in what colour, unioned or subtracted -- plus the mesh for each leaf. The product list
    refers to its leaves by the name their payload arrived under, so the parent resolves them from
    what it already has rather than from the filesystem.
    """

    def preview(self, source, output="preview.json"):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="preview", requestId=1,
                input=self.write_scad(source), output=output)
        payloads, done = self.read_until_done(parent)
        return process, payloads, done

    def test_preview_returns_a_product_list_and_its_leaves(self):
        process, payloads, done = self.preview("cube([10, 10, 10]);")
        self.assertTrue(done.get("ok"), f"preview failed: {done}")
        self.assertIn("preview.json", payloads, f"no product list; got {sorted(payloads)}")

        products = json.loads(payloads["preview.json"])
        self.assertIn("products", products)
        self.assertTrue(products["products"], "the product list is empty for a cube")

        leaves = [item for product in products["products"]
                  for item in product.get("intersections", [])]
        self.assertTrue(leaves, "the product has no intersections")
        for leaf in leaves:
            self.assertIn(leaf["geometry"], payloads,
                          f"product references {leaf['geometry']}, which never arrived")
            self.assertTrue(payloads[leaf["geometry"]].startswith(b"OSIG"))
            self.assertEqual(len(leaf["matrix"]), 16)
            self.assertEqual(len(leaf["color"]), 4)
            self.assertIn("convexity", leaf)

    def test_preview_carries_the_colour_a_model_asks_for(self):
        """color() is the reason leaf colour travels separately from the mesh -- losing it would
        make every preview monochrome."""
        _, payloads, done = self.preview("color([1, 0, 0]) cube(5);")
        self.assertTrue(done.get("ok"), f"preview failed: {done}")
        products = json.loads(payloads["preview.json"])
        colours = [item["color"] for product in products["products"]
                   for item in product.get("intersections", [])]
        self.assertTrue(any(c[0] == 1.0 and c[1] == 0.0 and c[2] == 0.0 for c in colours),
                        f"the requested colour is not in the product list: {colours}")

    def test_preview_keeps_subtractions_separate_from_intersections(self):
        """A difference() that arrived as a union would render as solid, which is the whole point
        of keeping the two chains apart."""
        _, payloads, done = self.preview("difference() { cube(10); sphere(6); }")
        self.assertTrue(done.get("ok"), f"preview failed: {done}")
        products = json.loads(payloads["preview.json"])
        subtractions = [item for product in products["products"]
                        for item in product.get("subtractions", [])]
        self.assertTrue(subtractions, "the subtracted sphere is missing from the product list")

    def test_preview_sends_each_distinct_leaf_once(self):
        """Two copies of one object share a mesh; sending it twice would double the bytes on the
        channel for no gain."""
        _, payloads, done = self.preview(
            "for (x = [0, 20, 40]) translate([x, 0, 0]) cube(5);")
        self.assertTrue(done.get("ok"), f"preview failed: {done}")
        products = json.loads(payloads["preview.json"])
        leaves = [item for product in products["products"]
                  for item in product.get("intersections", [])]
        self.assertGreaterEqual(len(leaves), 3, "expected one leaf per copy")
        geometry_payloads = [name for name in payloads if name != "preview.json"]
        self.assertEqual(len(set(geometry_payloads)), len(geometry_payloads))
        self.assertLess(len(geometry_payloads), len(leaves),
                        "identical cubes were sent as separate meshes")


@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerCacheLimits(WorkerFixture, unittest.TestCase):
    """The window's cache sizes have to reach the worker.

    The worker is a separate process, so the Preferences cache limits the window applies to itself
    never reach it: it ran with the built-in 100 MB each. A model bigger than that evicted cached
    geometry between previews, the worker recomputed it -- a recomputed hull can triangulate the
    same vertices differently -- and the window could not reuse any vertex buffers for those leaves.
    """

    def test_a_request_sets_the_worker_cache_limits(self):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="preview", requestId=1, input=self.write_scad("cube(1);"),
                output="preview.json", geometryCacheSizeMB=321, cgalCacheSizeMB=654)
        _, done = self.read_until_done(parent)
        self.assertTrue(done.get("ok"), f"preview failed: {done}")
        self.assertEqual(done.get("geometryCacheSizeMB"), 321, f"answer: {done}")
        self.assertEqual(done.get("cgalCacheSizeMB"), 654, f"answer: {done}")


@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerParameters(WorkerFixture, unittest.TestCase):
    """Customizer values have to reach the worker, or every render uses the file's defaults.

    A window that shows one thing and exports another is the failure this guards against: the
    values the user set in the Customizer are not in the .scad file, so unless the request carries
    them the worker cannot know about them.
    """

    MODEL = "size = 10;  // [1:100]\ncube(size);"

    def write_parameters(self, size, set_name="test"):
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        path = os.path.join(directory, "params.json")
        with open(path, "w") as handle:
            json.dump({"parameterSets": {set_name: {"size": str(size)}},
                       "fileFormatVersion": "1"}, handle)
        return path

    def render_with(self, **extra):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="render", requestId=1,
                input=self.write_scad(self.MODEL), output="result.osig", **extra)
        payloads, done = self.read_until_done(parent)
        return payloads, done

    def test_a_parameter_set_changes_the_geometry(self):
        default_payloads, default_done = self.render_with()
        self.assertTrue(default_done.get("ok"), f"default render failed: {default_done}")

        big_payloads, big_done = self.render_with(
            parameterFile=self.write_parameters(80), setName="test")
        self.assertTrue(big_done.get("ok"), f"parameterised render failed: {big_done}")

        # Same topology either way -- a cube is a cube -- so the payloads are the same length and
        # only the coordinates differ. Comparing bytes is what shows the value was applied.
        self.assertNotEqual(default_payloads["result.osig"], big_payloads["result.osig"],
                            "the parameter set did not change the geometry")

    def test_a_missing_parameter_set_behaves_as_it_does_on_the_command_line(self):
        """Naming a set that is not in the file falls back to the defaults, silently -- which is
        exactly what `openscad -p file -P absent` does today (verified: exit 0, no warning, output
        written). Arguably that should be an error, but changing it is a behaviour change to the
        Customizer rather than to process isolation, so what is pinned here is that the worker
        agrees with the command line. If the CLI is ever made stricter, this test should follow it
        rather than be deleted."""
        default_payloads, _ = self.render_with()
        payloads, done = self.render_with(
            parameterFile=self.write_parameters(80, set_name="other"), setName="absent")
        self.assertTrue(done.get("ok"))
        self.assertEqual(payloads["result.osig"], default_payloads["result.osig"],
                         "a missing set should leave the defaults in place, as the CLI does")

    def test_a_request_without_parameters_still_renders(self):
        _, done = self.render_with()
        self.assertTrue(done.get("ok"))



@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerWorkingDirectory(WorkerFixture, unittest.TestCase):
    """A window renders what is in the editor, which is not always what is on disk.

    The text is handed to the worker as a temporary file, so `include <>` and `use <>` would resolve
    relative to wherever that file landed rather than to the document's own directory -- and a model
    that renders in the GUI would fail in the worker. The request names the directory to resolve
    from instead.
    """

    def make_document(self):
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        with open(os.path.join(directory, "shape.scad"), "w") as handle:
            handle.write("module shape() { cube(7); }\n")
        return directory

    def render_from(self, source, document_directory=None):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        extra = {}
        if document_directory:
            # Where the document really lives. The text itself is somewhere else, exactly as a
            # window's unsaved editor contents would be.
            extra["sourcePath"] = os.path.join(document_directory, "model.scad")
        request(parent, command="render", requestId=1, input=self.write_scad(source),
                output="result.osig", **extra)
        return self.read_until_done(parent)

    SOURCE = 'include <shape.scad>\nshape();'

    def test_includes_resolve_against_the_working_directory(self):
        _, done = self.render_from(self.SOURCE, self.make_document())
        self.assertTrue(done.get("ok"), f"the include did not resolve: {done}")

    def test_without_a_working_directory_the_include_is_not_found(self):
        """The failure the field exists to prevent, pinned so the field cannot be quietly dropped."""
        self.make_document()
        _, done = self.render_from(self.SOURCE)
        self.assertFalse(done.get("ok"))

@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerIncludeReload(WorkerFixture, unittest.TestCase):
    """An edit to an included file has to reach the next render.

    The worker is persistent, and StatCache memoizes stat() by path with no invalidation of its
    own. SourceFileCache asks it whether an include has changed, so without clearing it between
    requests every later render of a document keeps the version of its includes that the first
    render saw -- the model on screen stops matching the files on disk, with nothing to say so.
    """

    def test_an_edited_include_is_picked_up_by_the_next_render(self):
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        include = os.path.join(directory, "shape.scad")
        model = os.path.join(directory, "model.scad")
        with open(model, "w") as handle:
            handle.write("include <shape.scad>\nshape();\n")

        def write_include(size):
            with open(include, "w") as handle:
                handle.write("module shape() { cube(%d); }\n" % size)
            # Coarse mtime resolution would otherwise hide the edit from a correct implementation
            # too, which would make this test pass for the wrong reason.
            stamp = os.path.getmtime(include) + 10
            os.utime(include, (stamp, stamp))

        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)

        write_include(7)
        request(parent, command="render", requestId=1, input=model, output="first.osig")
        first, done = self.read_until_done(parent)
        self.assertTrue(done.get("ok"), f"first render failed: {done}")

        write_include(30)
        request(parent, command="render", requestId=2, input=model, output="second.osig")
        second, done = self.read_until_done(parent)
        self.assertTrue(done.get("ok"), f"second render failed: {done}")

        self.assertNotEqual(first["first.osig"], second["second.osig"],
                            "the worker rendered the include it had already seen")

@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerFeatures(WorkerFixture, unittest.TestCase):
    """Experimental features are per-process, and the worker is a different process.

    The window enables them from Preferences; the worker starts with the defaults. So unless the
    request says which are on, a model using an experimental builtin renders as if that builtin did
    not exist -- the module is ignored with a warning and the result is an empty top-level object.
    A window then shows nothing and an F6 reports "evaluation failed", with the real reason only in
    a warning the user never sees. Process isolation is itself behind such a flag, so every
    isolated render is in exactly this position.
    """

    # roof() exists only when its feature is enabled, and it is not process-isolation's own flag,
    # so this keeps testing something after that one stabilises.
    MODEL = "roof() square(10);"

    def render(self, **extra):
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        request(parent, command="render", requestId=1, input=self.write_scad(self.MODEL),
                output="result.osig", **extra)
        return self.read_until_done(parent)

    def test_an_experimental_builtin_needs_its_feature_named(self):
        """The failure this guards: without the list, the module is unknown and nothing is built."""
        _, done = self.render()
        self.assertFalse(done.get("ok"),
                         f"expected an empty result without the feature enabled: {done}")

    def test_a_request_naming_the_feature_renders(self):
        payloads, done = self.render(features=["roof"])
        self.assertTrue(done.get("ok"), f"render failed with the feature enabled: {done}")
        self.assertIn("result.osig", payloads)


if __name__ == "__main__":
    unittest.main()
