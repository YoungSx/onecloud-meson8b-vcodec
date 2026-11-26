import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("measure", ROOT / "scripts/measure-pipeline.py")
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


class PipelineMeasurementTests(unittest.TestCase):
    @staticmethod
    def timeline():
        return {"streams": [
            {"index": 0, "codec_type": "video", "time_base": "1/1000"},
            {"index": 1, "codec_type": "audio", "time_base": "1/48000", "sample_rate": "48000"}],
            "frames": [
                {"stream_index": 0, "best_effort_timestamp": 1400, "duration": 40},
                {"stream_index": 1, "best_effort_timestamp": 67200, "nb_samples": 1920},
                {"stream_index": 0, "best_effort_timestamp": 1440, "pkt_duration": 40},
                {"stream_index": 1, "best_effort_timestamp": 69120, "nb_samples": 1920}]}

    def test_complete_audio_decode_uses_samples_and_original_timestamps(self):
        data = self.timeline()
        with mock.patch.object(measure.subprocess, "run", return_value=
                               subprocess.CompletedProcess([], 0, json.dumps(data), "")) as run:
            result = measure.verify_av(Path("output.m3u8"), "ffprobe", 0.08, 0.001, 3)
        self.assertEqual(result["audio"]["samples"], 3840)
        self.assertEqual(result["audio"]["sample_rate"], 48000)
        self.assertEqual(result["audio"]["duration_seconds"], 0.08)
        self.assertEqual(result["av_start_skew_seconds"], 0)
        self.assertEqual(result["av_end_skew_seconds"], 0)
        self.assertIn("-show_frames", run.call_args.args[0])
        self.assertNotIn("h264_v4l2m2m", run.call_args.args[0])

    def test_missing_audio_short_decode_and_gaps_cannot_pass(self):
        for fault in ("no-audio", "short-audio", "gap", "overlap", "duplicate-video",
                      "missing-pts", "missing-duration", "bad-timebase", "bad-rate"):
            with self.subTest(fault=fault):
                data = self.timeline()
                if fault == "no-audio":
                    data["streams"].pop()
                elif fault == "short-audio":
                    data["frames"].pop()
                elif fault == "gap":
                    data["frames"][-1]["best_effort_timestamp"] += 4800
                elif fault == "overlap":
                    data["frames"][-1]["best_effort_timestamp"] -= 480
                elif fault == "duplicate-video":
                    data["frames"][2]["best_effort_timestamp"] = 1400
                elif fault == "missing-pts":
                    del data["frames"][1]["best_effort_timestamp"]
                elif fault == "missing-duration":
                    del data["frames"][0]["duration"]
                elif fault == "bad-timebase":
                    data["streams"][0]["time_base"] = "1/0"
                elif fault == "bad-rate":
                    data["streams"][1]["sample_rate"] = "0"
                with self.assertRaises(ValueError):
                    measure.av_timeline(data, 0.08, 0.001)

    def test_audio_video_skew_and_decoder_errors_are_failures(self):
        data = self.timeline()
        for frame in data["frames"]:
            if frame["stream_index"] == 1:
                frame["best_effort_timestamp"] += 4800
        with self.assertRaisesRegex(ValueError, "skew"):
            measure.av_timeline(data, 0.08, 0.05)
        self.assertAlmostEqual(measure.av_timeline(data, 0.08, 0.11)["av_end_skew_seconds"], 0.1)
        # ffprobe can log a missing segment or recoverable decode error and exit zero.
        with mock.patch.object(measure.subprocess, "run", return_value=
                               subprocess.CompletedProcess([], 0, json.dumps(data), "missing segment")):
            with self.assertRaisesRegex(ValueError, "software decode failed"):
                measure.verify_av(Path("output.m3u8"), "ffprobe", 0.08, 0.11, 3)

    def test_counter_snapshot_ignores_unavailable_devices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "meson-vdec").mkdir()
            (root / "meson-vdec/convert_ns").write_text("17000\n")
            self.assertEqual(measure.counters(root), {"decoder.convert_ns": 17000})

    def test_successful_exit_still_rejects_short_or_wrong_content(self):
        rows = "0, 0, 0, 1, 100, abc\n0, 1, 1, 1, 100, def\n"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            reference = Path(directory) / "reference"
            output.write_bytes(b"compressed")
            reference.write_text(rows)
            with mock.patch.object(measure.subprocess, "run", return_value=
                                   subprocess.CompletedProcess([], 0, rows, "")) as run:
                self.assertEqual(measure.verify(output, "ffmpeg", 2, reference, 3)["frames"], 2)
                self.assertNotIn("h264_v4l2m2m", run.call_args.args[0])
                with self.assertRaisesRegex(ValueError, "2 of 3"):
                    measure.verify(output, "ffmpeg", 3, reference, 3)
                reference.write_text(rows.replace("def", "bad"))
                with self.assertRaisesRegex(ValueError, "content differs"):
                    measure.verify(output, "ffmpeg", 2, reference, 3)

    def test_timeout_stops_sampler_and_preserves_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(measure.subprocess, "run", side_effect=
                                   subprocess.TimeoutExpired(["slow"], 0.1)):
                result = measure.measure(["slow"], 0.1, root, root / "run.log")
            self.assertTrue(result["timed_out"])
            self.assertIsNone(result["returncode"])

    def test_reset_counter_cannot_report_false_improvement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(measure, "counters", side_effect=[{"x": 5}, {"x": 0}]), \
                 mock.patch.object(measure.subprocess, "run", return_value=
                                   subprocess.CompletedProcess([], 0)):
                with self.assertRaisesRegex(ValueError, "counter reset"):
                    measure.measure(["true"], 1, root, root / "run.log")
