#!/usr/bin/env python3
"""Bounded hardware-watchdog guard for risky bring-up tests.

Pets /dev/watchdog once a second while the default gateway has answered an
ICMP echo within --grace seconds. A kernel hang or a dead network stops the
petting and the SoC watchdog reboots the board. The process is memory-locked
and SCHED_FIFO, and probes without forking, so memory pressure or swap
cannot stall it into a spurious reboot. After --minutes, or on
SIGTERM/SIGINT, it disarms the watchdog with the magic close character.
"""
import argparse
import ctypes
import os
import signal
import socket
import struct
import sys
import threading
import time

MCL_CURRENT, MCL_FUTURE = 1, 2


def default_gateway():
    with open("/proc/net/route") as f:
        for line in f.readlines()[1:]:
            iface, dest, gw, flags = line.split()[:4]
            if dest == "00000000" and int(flags, 16) & 2:
                return socket.inet_ntoa(struct.pack("<I", int(gw, 16)))
    sys.exit("no default gateway")


def checksum(data):
    s = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    s = (s >> 16) + (s & 0xffff)
    return ~(s + (s >> 16)) & 0xffff


def probe_loop(gw, state, log):
    sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP)
    sock.settimeout(1.0)
    ident = os.getpid() & 0xffff
    seq = 0
    while not state["stop"]:
        seq = (seq + 1) & 0xffff
        hdr = struct.pack("!BBHHH", 8, 0, 0, ident, seq)
        sock.sendto(struct.pack("!BBHHH", 8, 0, checksum(hdr), ident, seq),
                    (gw, 0))
        deadline = time.monotonic() + 1.0
        ok = False
        while not ok and time.monotonic() < deadline:
            try:
                pkt, addr = sock.recvfrom(1024)
            except socket.timeout:
                break
            ihl = (pkt[0] & 0xf) * 4
            typ, _, _, rid, rseq = struct.unpack("!BBHHH", pkt[ihl:ihl + 8])
            ok = addr[0] == gw and typ == 0 and rid == ident and rseq == seq
        if ok:
            state["last_ok"] = time.monotonic()
        else:
            log(f"gateway miss, {time.monotonic() - state['last_ok']:.0f}s since reply")
        time.sleep(1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=20)
    ap.add_argument("--grace", type=float, default=30,
                    help="seconds without a gateway reply before reboot")
    ap.add_argument("--log", default="/root/venc-deploy/wdt-guard.log")
    args = ap.parse_args()

    gw = default_gateway()
    logf = open(args.log, "a", buffering=1)

    def log(msg):
        logf.write(f"{time.ctime()} {msg}\n")
        os.fsync(logf.fileno())

    state = {"stop": False, "last_ok": time.monotonic()}

    def on_signal(signum, frame):
        state["stop"] = True

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)
    # an OOM kill would close the device without the magic character
    with open("/proc/self/oom_score_adj", "w") as f:
        f.write("-1000")
    if ctypes.CDLL(None, use_errno=True).mlockall(MCL_CURRENT | MCL_FUTURE):
        sys.exit("mlockall failed")
    try:
        os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(10))
    except PermissionError:
        # no RT budget in this cgroup; paging stalls are covered by mlockall
        os.setpriority(os.PRIO_PROCESS, 0, -20)

    fd = os.open("/dev/watchdog", os.O_WRONLY)
    log(f"armed, gateway {gw}, {args.minutes} min, grace {args.grace}s")
    threading.Thread(target=probe_loop, args=(gw, state, log),
                     daemon=True).start()
    deadline = time.monotonic() + args.minutes * 60
    try:
        while not state["stop"] and time.monotonic() < deadline:
            if time.monotonic() - state["last_ok"] < args.grace:
                os.write(fd, b"\0")
            time.sleep(1.0)
    finally:
        state["stop"] = True
        os.write(fd, b"V")
        os.close(fd)
        log("disarmed")


if __name__ == "__main__":
    main()
