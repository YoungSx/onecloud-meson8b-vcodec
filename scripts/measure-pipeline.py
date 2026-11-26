#!/usr/bin/env python3
"""Measure one exact pipeline command and independently decode its output.

Use separate invocations for video stages and the complete application command,
optionally checking its audio timeline. No shell or service changes are performed.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import resource
import subprocess
import threading
import time

COUNTERS = {
    "decoder": ("converted_frames", "convert_ns", "cropped_frames", "ge2d_frames"),
    "encoder": ("copied_frames", "copy_ns", "direct_frames"),
}


def counters(root):
    values = {}
    for engine, names in COUNTERS.items():
        directory = root / ("meson-vdec" if engine == "decoder" else "meson-venc")
        for name in names:
            try:
                values[f"{engine}.{name}"] = int((directory / name).read_text())
            except (OSError, ValueError):
                pass
    return values


def memory():
    values = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        name, value = line.split(":", 1)
        if name in ("MemAvailable", "CmaFree"):
            values[name] = int(value.split()[0]) * 1024
    return values


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            sha.update(chunk)
    return sha.hexdigest()


def frame_hashes(text):
    rows = []
    for line in text.splitlines():
        if line and not line.startswith("#"):
            fields = [field.strip() for field in line.split(",")]
            if len(fields) != 6:
                raise ValueError("invalid framemd5 row")
            rows.append((int(fields[4]), fields[5]))
    return rows


def verify(output, ffmpeg, expected_frames, reference, timeout):
    # No hardware decoder selection: validate with the independent software path.
    result = subprocess.run(
        [ffmpeg, "-nostdin", "-v", "error", "-xerror", "-filter_threads", "1", "-i", str(output),
         "-map", "0:v:0", "-an", "-pix_fmt", "nv12", "-threads:v", "1",
         "-fps_mode", "passthrough", "-f", "framemd5", "-"],
        capture_output=True, text=True, timeout=timeout,
    )
    if result.returncode:
        raise ValueError("software decode failed: " + result.stderr[-2000:])
    rows = frame_hashes(result.stdout)
    if len(rows) != expected_frames:
        raise ValueError(f"software decoded {len(rows)} of {expected_frames} frames")
    if reference is not None and rows != frame_hashes(reference.read_text()):
        raise ValueError("frame content differs from the software reference")
    return {"frames": len(rows), "content_matches_reference": reference is not None,
            "output_sha256": digest(output)}


def av_timeline(data, audio_seconds, tolerance):
    streams = data.get("streams", [])
    frames = data.get("frames", [])
    spans = {}
    for kind in ("video", "audio"):
        stream = next((s for s in streams if s.get("codec_type") == kind), None)
        if stream is None:
            raise ValueError("missing " + kind + " stream")
        try:
            time_base = Fraction(stream["time_base"])
            rate = int(stream["sample_rate"]) if kind == "audio" else 1
        except (KeyError, ValueError, ZeroDivisionError) as error:
            raise ValueError("invalid " + kind + " time base or sample rate") from error
        if time_base <= 0 or rate <= 0:
            raise ValueError("invalid " + kind + " time base or sample rate")
        selected = [f for f in frames if f.get("stream_index") == stream["index"]]
        if not selected:
            raise ValueError("no decoded " + kind + " frames")
        start = end = previous = None
        samples = 0
        for frame in selected:
            try:
                pts = int(frame["best_effort_timestamp"]) * time_base
                if kind == "audio":
                    count = int(frame["nb_samples"])
                    duration = Fraction(count, rate)
                    samples += count
                else:
                    duration = int(frame.get("duration") or frame["pkt_duration"]) * time_base
            except (KeyError, ValueError) as error:
                raise ValueError("missing or invalid decoded " + kind + " timing") from error
            if duration <= 0:
                raise ValueError("nonpositive decoded " + kind + " duration")
            # Permit only one timestamp tick/sample of container rounding.
            rounding = max(time_base, Fraction(1, rate)) if kind == "audio" else 0
            if ((previous is not None and pts <= previous) or
                    (end is not None and abs(pts - end) > rounding)):
                raise ValueError("discontinuous " + kind + " timestamps")
            if start is None:
                start = pts
            end = pts + duration
            previous = pts
        spans[kind] = {"frames": len(selected), "start_seconds": float(start),
                       "end_seconds": float(end), "duration_seconds": float(end - start)}
        if kind == "audio":
            duration = Fraction(samples, rate)
            if abs(float(duration) - audio_seconds) > tolerance:
                raise ValueError(f"decoded audio duration {float(duration):.6f}s differs "
                                 f"from {audio_seconds:.6f}s")
            spans[kind].update(samples=samples, sample_rate=rate,
                               decoded_seconds=float(duration))
    start_skew = abs(spans["audio"]["start_seconds"] - spans["video"]["start_seconds"])
    end_skew = abs(spans["audio"]["end_seconds"] - spans["video"]["end_seconds"])
    if max(start_skew, end_skew) > tolerance:
        raise ValueError(f"audio/video skew exceeds {tolerance:.6f}s "
                         f"(start {start_skew:.6f}s, end {end_skew:.6f}s)")
    return {"audio": spans["audio"], "video_timeline": spans["video"],
            "audio_timeline_checked": True, "audio_content_checked": False,
            "av_start_skew_seconds": start_skew, "av_end_skew_seconds": end_skew,
            "timeline_tolerance_seconds": tolerance}


def verify_av(output, ffprobe, audio_seconds, tolerance, timeout):
    # show_frames decodes every segment, rather than trusting container durations.
    result = subprocess.run(
        [ffprobe, "-v", "error", "-show_frames", "-show_streams", "-show_entries",
         "stream=index,codec_type,time_base,sample_rate:"
         "frame=stream_index,best_effort_timestamp,nb_samples,pkt_duration,duration",
         "-of", "json", str(output)], capture_output=True, text=True, timeout=timeout,
    )
    if result.returncode or result.stderr.strip():
        raise ValueError("audio/video software decode failed: " + result.stderr[-2000:])
    return av_timeline(json.loads(result.stdout), audio_seconds, tolerance)


def measure(command, timeout, debugfs, log):
    before = counters(debugfs)
    minimum = memory()
    stopped = threading.Event()

    def sample():
        while not stopped.wait(0.1):
            for key, value in memory().items():
                minimum[key] = min(minimum.get(key, value), value)

    thread = threading.Thread(target=sample, daemon=True)
    thread.start()
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    try:
        with log.open("wb") as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=timeout)
        returncode = result.returncode
    except subprocess.TimeoutExpired:
        returncode = None
    finally:
        wall = time.monotonic() - started
        stopped.set()
        thread.join()
    after_usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    after = counters(debugfs)
    deltas = {}
    for name, old in before.items():
        if name in after:
            if after[name] < old:
                raise ValueError("driver counter reset during measurement: " + name)
            deltas[name] = after[name] - old
    return {
        "argv": command, "returncode": returncode, "timed_out": returncode is None,
        "wall_seconds": wall,
        "child_cpu_seconds": after_usage.ru_utime + after_usage.ru_stime
                             - usage.ru_utime - usage.ru_stime,
        "minimum_memory_bytes": minimum, "driver_counter_deltas": deltas,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--media-seconds", type=float, required=True)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--audio-seconds", type=float,
                        help="also decode audio and check its duration and A/V timeline")
    parser.add_argument("--timeline-tolerance", type=float, default=0.05,
                        help="maximum audio duration error and A/V skew in seconds (default: 0.05)")
    parser.add_argument("--debugfs", type=Path, default=Path("/sys/kernel/debug"))
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if (not command or args.frames < 1 or
            any(not math.isfinite(value) or value <= 0
                for value in (args.media_seconds, args.timeout))):
        parser.error("a command, positive frame count, duration and timeout are required")
    if (not math.isfinite(args.timeline_tolerance) or args.timeline_tolerance < 0 or
            (args.audio_seconds is not None and
             (not math.isfinite(args.audio_seconds) or args.audio_seconds <= 0))):
        parser.error("audio duration must be positive and timeline tolerance nonnegative")
    if args.output.exists():
        parser.error("output must be a new file, to exclude a stale successful result")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    report = {"stage": args.stage, "status": "failed", "audio_timeline_checked": False}
    try:
        report.update(measure(command, args.timeout, args.debugfs,
                              args.report.with_suffix(".log")))
        if report["returncode"] != 0:
            raise ValueError("pipeline command failed or timed out")
        report.update(verify(args.output, args.ffmpeg, args.frames, args.reference,
                             args.timeout))
        if args.audio_seconds is not None:
            report.update(verify_av(args.output, args.ffprobe, args.audio_seconds,
                                    args.timeline_tolerance, args.timeout))
        report["speed"] = args.media_seconds / report["wall_seconds"]
        report["status"] = "passed"
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{args.stage}: {report['status']}")
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
