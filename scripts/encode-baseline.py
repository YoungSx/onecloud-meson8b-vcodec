#!/usr/bin/env python3
"""Record H.264 hardware encoder baselines on the OneCloud target.

Runs tests/encode-test (built for the target) over sizes, synthetic patterns,
input formats and control modes, then FFmpeg h264_v4l2m2m versus libx264 on
testsrc2. It never loads modules, changes firmware or stops services; run it
detached (nohup) so the management SSH session is not part of the load.

Per run: process CPU (child rusage), system CPU increment over a measured
idle rate (includes kworker), wall time, per-frame size/key/latency,
average and 1 s window peak bitrate, minimum MemAvailable/CmaFree and
temperature. Quality is PSNR against the client's -g reference, piped into
FFmpeg so no raw video is stored on the target.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import statistics
import subprocess
import tempfile
import threading
import time

FRAME_LINE = re.compile(r"^frame (\d+) bytes (\d+) flags (0x[0-9a-f]+|0) latency_us (\d+)$")
KEYFRAME = 0x8
CLOCK_TICKS = os.sysconf("SC_CLK_TCK")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def cpu_ticks():
    """Busy and total jiffies of the aggregate cpu line."""
    fields = [int(v) for v in Path("/proc/stat").read_text().split("\n", 1)[0].split()[1:9]]
    user, nice, system, idle, iowait, irq, softirq, steal = fields
    busy = user + nice + system + irq + softirq + steal
    return busy, busy + idle + iowait


def meminfo():
    values = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        key, value = line.split(":", 1)
        if key in ("MemAvailable", "CmaFree", "CmaTotal"):
            values[key] = int(value.split()[0]) * 1024
    return values


def temperatures():
    values = []
    for path in sorted(Path("/sys/class/thermal").glob("thermal_zone*/temp")):
        try:
            values.append(int(path.read_text()))
        except (OSError, ValueError):
            pass
    return values
class Sampler(threading.Thread):
    """Minimum available memory/CMA and maximum temperature at 10 Hz."""

    def __init__(self):
        super().__init__(daemon=True)
        self.stop = threading.Event()
        self.minimum = meminfo()
        self.temperature = max(temperatures() or [0])

    def run(self):
        while not self.stop.wait(0.1):
            for key, value in meminfo().items():
                self.minimum[key] = min(self.minimum.get(key, value), value)
            self.temperature = max([self.temperature] + temperatures())

    def result(self):
        self.stop.set()
        self.join()
        return {"min_mem_available": self.minimum.get("MemAvailable"),
                "min_cma_free": self.minimum.get("CmaFree"),
                "max_millidegrees": self.temperature}


def measured(args, timeout, idle_seconds, stdin=None, stdout=subprocess.DEVNULL):
    """Run one command; CPU is the child's rusage plus the system increment."""
    busy0, total0 = cpu_ticks()
    time.sleep(idle_seconds)
    busy1, total1 = cpu_ticks()
    idle_rate = (busy1 - busy0) / max(total1 - total0, 1)
    sampler = Sampler()
    sampler.start()
    usage0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    result = subprocess.run(args, stdin=stdin, stdout=stdout, stderr=subprocess.PIPE,
                            universal_newlines=True, timeout=timeout)
    wall = time.monotonic() - started
    usage1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    busy2, total2 = cpu_ticks()
    metrics = {
        "returncode": result.returncode,
        "wall_seconds": round(wall, 3),
        "process_cpu_seconds": round(usage1.ru_utime - usage0.ru_utime +
                                     usage1.ru_stime - usage0.ru_stime, 3),
        "process_max_rss_kib": usage1.ru_maxrss,
        # Busy jiffies above the background rate measured just before the run.
        "system_cpu_increment_seconds": round(
            ((busy2 - busy1) - idle_rate * (total2 - total1)) / CLOCK_TICKS, 3),
        "background_busy_fraction": round(idle_rate, 4),
    }
    metrics.update(sampler.result())
    return result, metrics


def frame_stats(stderr, frames, fps):
    rows = [FRAME_LINE.match(line) for line in stderr.splitlines()]
    rows = [(int(m[1]), int(m[2]), int(m[3], 16), int(m[4])) for m in rows if m]
    if [row[0] for row in rows] != list(range(frames)):
        raise RuntimeError("client reported frames %d of %d" % (len(rows), frames))
    sizes = [row[1] for row in rows]
    latency = sorted(row[3] for row in rows)
    window = max(1, round(fps))
    peak = max(sum(sizes[i:i + window]) for i in range(max(1, len(sizes) - window + 1)))
    loop = re.search(r"loop_us (\d+)", stderr)
    return {
        "frames": len(rows),
        "bytes": sum(sizes),
        "keyframes": [row[0] for row in rows if row[2] & KEYFRAME],
        "frame_bytes": sizes,
        "average_kbps": round(sum(sizes) * 8 * fps / len(sizes) / 1000, 1),
        "peak_1s_kbps": round(peak * 8 * fps / window / 1000, 1),
        "latency_us": {"median": latency[len(latency) // 2],
                       "p95": latency[min(len(latency) - 1, (len(latency) * 95 + 99) // 100 - 1)],
                       "max": latency[-1]},
        "client_fps": round(frames / (int(loop[1]) / 1e6), 2) if loop else None,
    }
def read_psnr(path, frames):
    rows = [dict(item.split(":", 1) for item in line.split())
            for line in path.read_text().splitlines() if line.strip()]
    if len(rows) != frames:
        raise RuntimeError("PSNR compared %d of %d frames" % (len(rows), frames))
    # Identical frames report inf; cap so the JSON stays finite.
    y = [min(float(row["psnr_y"]), 99.0) for row in rows]
    average = [min(float(row["psnr_avg"]), 99.0) for row in rows]
    return {"psnr_y_mean": round(statistics.mean(y), 3), "psnr_y_min": round(min(y), 3),
            "psnr_avg_mean": round(statistics.mean(average), 3),
            "psnr_avg_min": round(min(average), 3), "frame_psnr_y": [round(v, 2) for v in y]}


def psnr(args, pattern, width, height, stream):
    """Decoded frames versus the client's -g reference; both retimed to fps."""
    fps = args.fps
    with tempfile.TemporaryDirectory(prefix="meson8b-psnr-") as temporary:
        stats = Path(temporary) / "psnr.log"
        generator = subprocess.Popen(
            [args.client, "-g", "-p", pattern, "-", "-", "-", str(width), str(height),
             str(args.frames), "NV12", "90", "H264"], stdout=subprocess.PIPE)
        try:
            result = subprocess.run(
                [args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
                 "-f", "rawvideo", "-pix_fmt", "nv12", "-s", "%dx%d" % (width, height),
                 "-r", str(fps), "-i", "-", "-r", str(fps), "-f", "h264", "-i", str(stream),
                 "-lavfi", "[1:v]setpts=N/(%s*TB),format=nv12[d];[0:v]setpts=N/(%s*TB)[r];"
                 "[d][r]psnr=stats_file=%s:shortest=1" % (fps, fps, stats),
                 "-f", "null", "-"], stdin=generator.stdout, stderr=subprocess.PIPE,
                universal_newlines=True, timeout=args.timeout)
        finally:
            generator.stdout.close()
            generator.wait(timeout=60)
        if result.returncode or not stats.exists():
            raise RuntimeError("PSNR failed: " + result.stderr[-1000:])
        return read_psnr(stats, args.frames)


def expand(control, width, height, fps, fixed_kbps):
    """{bpp:X}: X bits per pixel at the run's size and rate; {fixed:X}: X times
    the median rate the same case reached in mode "fixed" (run that mode first)."""
    def fixed(match):
        if fixed_kbps is None:
            raise ValueError("{fixed:X} needs a preceding passing fixed mode")
        return str(round(fixed_kbps * 1000 * float(match[1])))
    control = re.sub(r"\{bpp:([0-9.]+)\}",
                     lambda m: str(round(width * height * fps * float(m[1]))), control)
    return re.sub(r"\{fixed:([0-9.]+)\}", fixed, control)


def expected_keyframes(controls, frames):
    gop = 12
    for control in controls:
        name, _, value = control.partition("=")
        if name == "force-key":
            return None
        if name == "gop" and "@" not in value:
            gop = int(value, 0)
    return list(range(0, frames, gop)) if gop > 0 else None


def summarize(runs):
    keys = ["wall_seconds", "process_cpu_seconds", "system_cpu_increment_seconds",
            "client_fps", "average_kbps", "peak_1s_kbps", "min_mem_available",
            "min_cma_free", "max_millidegrees", "psnr_y_mean", "psnr_y_min",
            "psnr_avg_mean"]
    summary = {}
    for key in keys:
        values = [run[key] for run in runs if run.get(key) is not None]
        if values:
            summary[key] = {"min": min(values), "median": statistics.median(values),
                            "max": max(values)}
    for key in ("median", "p95", "max"):
        values = [run["latency_us"][key] for run in runs if "latency_us" in run]
        if values:
            summary["latency_us_" + key] = {"min": min(values),
                                            "median": statistics.median(values),
                                            "max": max(values)}
    summary["deterministic"] = len({run.get("stream_sha256") for run in runs}) == 1
    return summary
def client_case(args, width, height, pattern, pixel_format, label, controls, fixed_kbps):
    case = {"label": label, "size": "%dx%d" % (width, height), "pattern": pattern,
            "format": pixel_format, "controls": controls, "runs": []}
    try:
        controls = [expand(c, width, height, args.fps, fixed_kbps) for c in controls]
    except ValueError as error:
        case.update(runs=[{"error": str(error)}], summary={}, **{"pass": False})
        return case
    case["controls"] = controls
    command = [args.client, "-p", pattern, "-r", str(args.fps)]
    for control in controls:
        command += ["-c", control]
    for index in range(args.runs):
        stream = args.work / "stream.h264"
        run = {}
        try:
            result, run = measured(command + [args.device, str(stream), "/dev/null",
                                              str(width), str(height), str(args.frames),
                                              pixel_format, "90", "H264"],
                                   args.timeout, args.idle_seconds)
            if result.returncode or "PASS:" not in result.stderr:
                raise RuntimeError("client failed: " + result.stderr[-1000:])
            run.update(frame_stats(result.stderr, args.frames, args.fps))
            expected = expected_keyframes(controls, args.frames)
            run["keyframes_ok"] = expected is None or run["keyframes"] == expected
            run["stream_sha256"] = sha256(stream)
            # Motion search depends on DRAM timing, so every run is scored.
            run.update(psnr(args, pattern, width, height, stream))
            if index:
                del run["frame_bytes"], run["frame_psnr_y"]
        except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
            run["error"] = str(error)
        finally:
            if stream.exists():
                stream.unlink()
        case["runs"].append(run)
    case["summary"] = summarize([run for run in case["runs"] if "error" not in run])
    case["pass"] = all("error" not in run and run["keyframes_ok"] for run in case["runs"])
    return case


def ffmpeg_cases(args):
    """Standard-client continuity with the 2026-11-13 testsrc2 records."""
    source = "testsrc2=size=1920x1080:rate=30"
    cases = []
    for name, codec in (("h264_v4l2m2m", ["-c:v", "h264_v4l2m2m", "-b:v", "0"]),
                        ("libx264", ["-c:v", "libx264", "-preset", "ultrafast"])):
        case = {"label": "ffmpeg-" + name, "size": "1920x1080", "pattern": "testsrc2",
                "format": "NV12", "controls": codec, "runs": []}
        stream = args.work / "ffmpeg.h264"
        for index in range(args.ffmpeg_runs):
            run = {}
            try:
                result, run = measured(
                    [args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
                     "-f", "lavfi", "-i", source, "-frames:v", str(args.frames),
                     "-pix_fmt", "nv12"] + codec + ["-f", "h264", "-y", str(stream)],
                    args.timeout, args.idle_seconds)
                if result.returncode:
                    raise RuntimeError(result.stderr[-1000:])
                run["bytes"] = stream.stat().st_size
                run["average_kbps"] = round(run["bytes"] * 8 * 30 / args.frames / 1000, 1)
                run["stream_sha256"] = sha256(stream)
                with tempfile.TemporaryDirectory(prefix="meson8b-psnr-") as temporary:
                    stats = Path(temporary) / "psnr.log"
                    check = subprocess.run(
                        [args.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
                         "-r", "30", "-f", "h264", "-i", str(stream), "-f", "lavfi",
                         "-i", source, "-lavfi", "[0:v]setpts=N/(30*TB),format=yuv420p[a];"
                         "[1:v]format=yuv420p[b];[a][b]psnr=stats_file=%s:shortest=1" % stats,
                         "-f", "null", "-"], stderr=subprocess.PIPE,
                        universal_newlines=True, timeout=args.timeout)
                    if check.returncode:
                        raise RuntimeError("PSNR failed: " + check.stderr[-1000:])
                    run.update(read_psnr(stats, args.frames))
                if index:
                    del run["frame_psnr_y"]
            except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
                run["error"] = str(error)
            finally:
                if stream.exists():
                    stream.unlink()
            case["runs"].append(run)
        case["summary"] = summarize([run for run in case["runs"] if "error" not in run])
        case["pass"] = all("error" not in run for run in case["runs"])
        cases.append(case)
    return cases
def parse_size(text):
    width, height = (int(v) for v in text.lower().split("x"))
    if width < 16 or height < 16 or width % 2 or height % 2:
        raise argparse.ArgumentTypeError("invalid size " + text)
    return width, height


def parse_mode(text):
    label, separator, controls = text.partition("=")
    if not label or not separator:
        raise argparse.ArgumentTypeError("mode must be LABEL=NAME=VALUE[,NAME=VALUE...]")
    return label, [c for c in controls.split(",") if c]


def environment(args):
    def read(path):
        try:
            return Path(path).read_text().strip()
        except OSError:
            return None
    info = {"kernel": platform.release(), "client_sha256": sha256(args.client),
            "governor": read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
            "max_khz": read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"),
            "ffmpeg_sha256": sha256(args.ffmpeg)}
    for module in args.module:
        info["module_sha256:" + Path(module).name] = sha256(module)
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", required=True)
    parser.add_argument("--device", default="/dev/video1")
    parser.add_argument("--ffmpeg", default="/usr/local/bin/ffmpeg")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True,
                        help="disk directory for one stream at a time, not tmpfs")
    parser.add_argument("--sizes", type=lambda s: [parse_size(v) for v in s.split(",")],
                        default=[(320, 240), (1280, 720), (1920, 1080)])
    parser.add_argument("--patterns", default="static,motion,noise,scene")
    parser.add_argument("--formats", default="NV12")
    parser.add_argument("--mode", type=parse_mode, action="append", dest="modes",
                        help="LABEL=NAME=VALUE[,...]; {bpp:X} expands to a bitrate")
    parser.add_argument("--frames", type=int, default=150)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--ffmpeg-runs", type=int, default=3)
    parser.add_argument("--idle-seconds", type=float, default=2.0)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--module", action="append", default=[],
                        help="loaded module file whose hash is recorded")
    args = parser.parse_args()
    if min(args.frames, args.fps, args.runs) < 1 or args.ffmpeg_runs < 0:
        raise SystemExit("frames, fps and runs must be positive")
    modes = args.modes or [("fixed", ["bitrate=0"]), ("cbr", ["bitrate={bpp:0.07}"])]
    args.work.mkdir(parents=True, exist_ok=True)
    report = {"parameters": {"frames": args.frames, "fps": args.fps, "runs": args.runs,
                             "idle_seconds": args.idle_seconds, "device": args.device,
                             "modes": dict(modes)},
              "environment": environment(args), "started": time.strftime("%FT%T%z"),
              "cases": []}
    for width, height in args.sizes:
        for pattern in args.patterns.split(","):
            for pixel_format in args.formats.split(","):
                fixed_kbps = None
                for label, controls in modes:
                    case = client_case(args, width, height, pattern, pixel_format, label,
                                       controls, fixed_kbps)
                    if label == "fixed" and case["pass"]:
                        fixed_kbps = case["summary"]["average_kbps"]["median"]
                    report["cases"].append(case)
                    args.output.write_text(json.dumps(report, indent=1) + "\n")
    if args.ffmpeg_runs:
        report["cases"] += ffmpeg_cases(args)
    report["finished"] = time.strftime("%FT%T%z")
    report["pass"] = all(case["pass"] for case in report["cases"])
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    print("%s: %d cases, report %s" % ("PASS" if report["pass"] else "FAIL",
                                       len(report["cases"]), args.output))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
