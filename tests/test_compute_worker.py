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
            extra["workingDirectory"] = document_directory
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


if __name__ == "__main__":
    unittest.main()
