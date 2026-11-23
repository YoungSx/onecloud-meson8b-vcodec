import importlib.util
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
