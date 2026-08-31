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

    def test_preview_is_reported_as_not_implemented_yet(self):
        """Preview returns a CSG product list rather than a mesh, and that is not built. Saying so
        is better than quietly serving a render and letting the caller believe otherwise."""
        _, _, (name, body) = self.exchange(command="preview", requestId=3)
        self.assertEqual(name, "done")
        self.assertFalse(body.get("ok"))
        self.assertIn("error", body)

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


if __name__ == "__main__":
    unittest.main()
