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
import socket
import subprocess
import sys
import unittest

from ipc_channel import read_json_message, read_message, request, write_message

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
        request(parent, **fields)
        reply = read_json_message(parent)
        self.assertIsNotNone(reply, "the worker closed the channel instead of answering")
        return process, parent, reply

    def test_worker_answers_a_request(self):
        _, _, (name, body) = self.exchange(command="preview", requestId=7)
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
        name, body = read_json_message(parent)
        self.assertEqual(name, "done")
        self.assertFalse(body.get("ok"))
        self.assertIsNone(process.poll())

    def test_worker_ignores_a_message_it_does_not_know(self):
        """Forward compatibility: a name from a newer parent must not wedge an older worker."""
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        write_message(parent, "something-from-the-future", b"\x00\x01")
        request(parent, command="preview", requestId=2)
        name, body = read_json_message(parent)
        self.assertEqual(name, "done")
        self.assertEqual(body.get("requestId"), 2)

    def test_worker_serves_several_requests_in_order(self):
        """The worker is persistent -- that is what makes a repeat render cheap. Each request is
        answered, in order, over one connection."""
        process, parent = self.start_worker()
        parent.settimeout(REPLY_TIMEOUT)
        for identifier in range(4):
            request(parent, command="preview", requestId=identifier)
            name, body = read_json_message(parent)
            self.assertEqual(name, "done")
            self.assertEqual(body.get("requestId"), identifier)
        self.assertIsNone(process.poll())


if __name__ == "__main__":
    unittest.main()
