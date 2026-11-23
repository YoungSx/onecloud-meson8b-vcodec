#!/usr/bin/env python3
"""Bounded passive outage evidence; never reconfigure or restart the target."""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import time


def read(path):
    try:
        return path.read_text().strip()
    except OSError as error:
        return {"errno": error.errno}


def gateway(proc, interface):
    for line in (proc / "net/route").read_text().splitlines()[1:]:
        fields = line.split()
        if fields[0] == interface and fields[1] == "00000000" and int(fields[3], 16) & 2:
            return socket.inet_ntoa(struct.pack("<I", int(fields[2], 16)))
    raise ValueError("no default gateway on " + interface)


def ping(address):
    try:
        result = subprocess.run(
            ["ping", "-n", "-c", "1", "-W", "1", address],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2,
        )
        return {"returncode": result.returncode}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"error": type(error).__name__}


def space(path):
    try:
        stat = os.statvfs(path)
        return {"bytes_available": stat.f_bavail * stat.f_frsize,
                "bytes_free_including_reserved": stat.f_bfree * stat.f_frsize,
                "bytes_total": stat.f_blocks * stat.f_frsize,
                "inodes_available": stat.f_favail}
    except OSError as error:
        return {"errno": error.errno}


def snapshot(interface, proc=Path("/proc"), sysfs=Path("/sys"), storage=Path("/")):
    net = sysfs / "class/net" / interface
    result = {
        "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "monotonic": time.monotonic(),
        "boot_id": read(proc / "sys/kernel/random/boot_id"),
        "interface": interface,
        "proc": {name: read(proc / name) for name in (
            "uptime", "loadavg", "meminfo", "interrupts", "net/softnet_stat", "diskstats",
        )},
        "link": {name: read(net / name) for name in ("carrier", "operstate")},
        "statistics": {p.name: read(p) for p in sorted((net / "statistics").glob("*"))},
        "filesystems": {"root": space(Path("/")), "storage": space(storage)},
    }
    # Keep cumulative counters; deltas survive missed samples and reader restarts.
    for name, prefixes in (("vmstat", ("pg", "pswp", "allocstall", "oom_kill")),
                           ("stat", ("cpu", "procs_", "intr ", "softirq "))):
        data = read(proc / name)
        result["proc"][name] = (
            "\n".join(line for line in data.splitlines() if line.startswith(prefixes))
            if isinstance(data, str) else data
        )
    try:
        result["gateway"] = ping(gateway(proc, interface))
    except (OSError, ValueError) as error:
        result["gateway"] = {"error": type(error).__name__}
    return result


def append(path, record, limit=4 * 1024 * 1024):
    data = (json.dumps(record, separators=(",", ":")) + "\n").encode()
    if len(data) > limit:
        raise ValueError("sample exceeds log size limit")
    if path.exists() and path.stat().st_size + len(data) > limit:
        path.replace(path.with_name(path.name + ".previous"))
    # Refuse symlinks and keep raw host diagnostics private, including old files.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "ab") as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", default="eth0")
    parser.add_argument("--seconds", type=positive, default=1800)
    parser.add_argument("--interval", type=positive, default=10)
    parser.add_argument("--log", type=Path, required=True,
                        help="private file in an existing persistent directory; two 4 MiB logs")
    parser.add_argument("--storage", type=Path,
                        help="also record free space/inodes on this mount (default: log directory)")
    args = parser.parse_args()
    if args.interface not in os.listdir("/sys/class/net"):
        parser.error("unknown interface")
    if args.log.is_symlink() or not args.log.parent.is_dir():
        parser.error("log requires a regular path in an existing directory")
    stop = False

    def stopping(signum, frame):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGTERM, stopping)
    signal.signal(signal.SIGINT, stopping)
    deadline = time.monotonic() + args.seconds
    while not stop and time.monotonic() < deadline:
        started = time.monotonic()
        append(args.log, snapshot(args.interface, storage=args.storage or args.log.parent))
        next_sample = min(started + args.interval, deadline)
        while not stop and time.monotonic() < next_sample:
            time.sleep(min(0.25, max(0, next_sample - time.monotonic())))


if __name__ == "__main__":
    main()
