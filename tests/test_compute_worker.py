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

import os
import socket
import subprocess
import sys
import unittest

CHANNEL_VARIABLE = "OPENSCAD_IPC_CHANNEL"
TIMEOUT = 30


def openscad_binary():
    """Deliberately fails rather than skipping. A skip counts as a pass, so a missing binary would
    let this whole file quietly verify nothing -- which is indistinguishable from working."""
    path = os.environ.get("OPENSCAD_BINARY")
    if not path:
        raise AssertionError("OPENSCAD_BINARY is not set; ctest sets it from OPENSCAD_BINPATH")
    if not os.path.exists(path):
        raise AssertionError(f"OPENSCAD_BINARY does not exist: {path}")
    return path


@unittest.skipIf(sys.platform == "win32", "descriptor passing is POSIX-only; see module docstring")
class ComputeWorkerEntryPoint(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
