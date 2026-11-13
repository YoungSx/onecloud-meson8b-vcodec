#!/usr/bin/env python3
"""Unattended encode benchmark (P3-12): HCODEC h264_v4l2m2m vs libx264.

Run detached on the target via venc-run.sh (watchdog guard armed,
no SSH session attached): CPU numbers are child-process times from
os.times(), so the DERP-relay management session cannot pollute them.
"""
import json
import os
import pathlib
import statistics
import subprocess
import time

D = pathlib.Path("/root/venc-deploy")
LOG = open(D / "enc-bench.log", "a", buffering=1)
FF = "/usr/local/bin/ffmpeg"
SRC = "testsrc2=size=1920x1080:rate=30:duration=5"  # 150 frames
RUNS = 3


def log(*a):
    print(*a, file=LOG, flush=True)


def timed(cmd, timeout_s):
    """Run cmd under timeout, return result + child user/sys/wall seconds."""
    t0, proc0 = time.monotonic(), os.times()
    r = subprocess.run(["timeout", "-s", "KILL", str(timeout_s)] + cmd,
                       capture_output=True, text=True)
    dt, t1 = time.monotonic() - t0, os.times()
    return r, {
        "rc": r.returncode,
        "wall_s": round(dt, 2),
        "user_s": round(t1.children_user - proc0.children_user, 2),
        "sys_s": round(t1.children_system - proc0.children_system, 2),
    }


def main():
    dmesg_mark = len(subprocess.check_output(["dmesg"], text=True, timeout=5).splitlines())
    subprocess.run(["modprobe", "v4l2_mem2mem"], check=True)
    subprocess.run(["modprobe", "videobuf2_vmalloc"], check=True)
    loaded = open("/proc/modules").read()
    for mod in ("meson8b_runtime", "meson-venc"):
        if mod.replace("-", "_") not in loaded:
            subprocess.run(["insmod", str(D / (mod + ".ko"))], check=True)

    log(f"=== {time.strftime('%F %T')} encode bench 1080p30 x150")
    res = {}
    for tag, codec in (("hw", ["-c:v", "h264_v4l2m2m"]),
                       ("sw", ["-c:v", "libx264", "-preset", "ultrafast"])):
        runs = []
        for i in range(RUNS):
            out = f"/tmp/enc-bench-{tag}.h264"
            r, m = timed([FF, "-nostdin", "-hide_banner", "-loglevel", "error",
                          "-f", "lavfi", "-i", SRC, "-pix_fmt", "nv12", *codec,
                          "-f", "h264", "-y", out], 240)
            m["bytes"] = os.path.getsize(out) if os.path.exists(out) else 0
            runs.append(m)
            log(f"  {tag} run{i}: {m}")
        res[tag] = runs

    # Quality: PSNR of each encoder's last output against the source. The raw
    # H.264 elementary stream carries no timestamps, so ffmpeg's decoder
    # defaults it to 25 fps and framesync would pair the wrong frames; retime
    # it to the source's 30 fps before comparing (a naive comparison reads
    # ~23 dB, retimed reads the real ~45 dB).
    for tag in ("hw", "sw"):
        r, m = timed([FF, "-nostdin", "-hide_banner", "-loglevel", "info",
                      "-i", f"/tmp/enc-bench-{tag}.h264", "-f", "lavfi", "-i", SRC,
                      "-lavfi", "[0:v]setpts=N/(30*TB),format=yuv420p[a];"
                                "[1:v]format=yuv420p[b];[a][b]psnr",
                      "-f", "null", "-"], 300)
        lines = [l for l in (r.stderr or "").splitlines() if "PSNR" in l]
        res[tag + "_psnr"] = lines[-1].split("Average ")[-1] if lines else f"MISSING rc={r.returncode}"
        log(f"  {tag} psnr: {res[tag + '_psnr']}")

    for tag in ("hw", "sw"):
        res[tag + "_median_cpu_s"] = statistics.median(
            r["user_s"] + r["sys_s"] for r in res[tag])
        res[tag + "_median_fps"] = round(150 / statistics.median(
            r["wall_s"] for r in res[tag]), 1)
    res["cpu_reduction_fraction"] = round(
        1 - res["hw_median_cpu_s"] / res["sw_median_cpu_s"], 3)

    if subprocess.run(["ping", "-c3", "-W1", "192.168.31.1"],
                      capture_output=True).returncode:
        log("  GATEWAY LOST")
        res["gateway"] = "LOST"
    else:
        res["gateway"] = "ok"

    new_dmesg = subprocess.check_output(["dmesg"], text=True, timeout=5).splitlines()[dmesg_mark:]
    res["kernel_warnings"] = [l for l in new_dmesg
                              if any(s in l for s in ("BUG:", "WARNING:", "Oops:", "refcount_t:"))]

    (D / "enc-bench.json").write_text(json.dumps(res, indent=2))
    log(f"=== done hw={res['hw_median_cpu_s']}s sw={res['sw_median_cpu_s']}s "
        f"cpu-{int(res['cpu_reduction_fraction'] * 100)}% hw_fps={res['hw_median_fps']}")
    assert all(r["rc"] == 0 for tag in ("hw", "sw") for r in res[tag]), "encode run failed"
    assert res["gateway"] == "ok", "gateway lost"
    assert not res["kernel_warnings"], res["kernel_warnings"]


if __name__ == "__main__":
    main()
