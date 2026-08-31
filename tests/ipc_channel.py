"""Python end of the compute worker's framed channel (see src/io/ipc_channel.h).

Two little-endian 64-bit lengths -- name, then payload -- followed by the bytes of each. That is
the whole format; it exists because a stream socket has no message boundaries, not because the
payloads are awkward.
"""

import json
import struct

HEADER = struct.Struct("<QQ")


def write_message(sock, name, payload=b""):
    if isinstance(name, str):
        name = name.encode()
    if isinstance(payload, str):
        payload = payload.encode()
    sock.sendall(HEADER.pack(len(name), len(payload)) + name + payload)


def _recv_exactly(sock, count):
    chunks = []
    remaining = count
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            return None  # peer finished, cleanly or otherwise
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_message(sock):
    """Returns (name, payload), or None at the end of the channel."""
    header = _recv_exactly(sock, HEADER.size)
    if header is None:
        return None
    name_size, payload_size = HEADER.unpack(header)
    name = _recv_exactly(sock, name_size) if name_size else b""
    if name is None:
        return None
    payload = _recv_exactly(sock, payload_size) if payload_size else b""
    if payload is None:
        return None
    return name.decode(), payload


def request(sock, **fields):
    write_message(sock, "request", json.dumps(fields))
