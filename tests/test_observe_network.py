"""Outage samples remain bounded, private and useful with missing metrics."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "observe_network", Path(__file__).parents[1] / "scripts/observe-network.py"
)
OBSERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OBSERVER)


class ObserverTests(unittest.TestCase):
    def test_rotation_preserves_last_two_records_privately(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.jsonl"
            path.touch(mode=0o644)
            for number in range(4):
                OBSERVER.append(path, {"sample": number}, limit=20)
            self.assertEqual(json.loads(path.read_text()), {"sample": 3})
            previous = path.with_name(path.name + ".previous")
            self.assertEqual(json.loads(previous.read_text()), {"sample": 2})
            for file in (path, previous):
                self.assertEqual(file.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(ValueError):
                OBSERVER.append(path, {"too_big": "x" * 30}, limit=20)
            self.assertEqual(json.loads(path.read_text()), {"sample": 3})

    def test_symlink_never_appends_to_another_file(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / "original"
            original.write_text("keep")
            link = Path(directory) / "link"
            link.symlink_to(original)
            with self.assertRaises(OSError):
                OBSERVER.append(link, {})
            self.assertEqual(original.read_text(), "keep")

    def test_missing_metrics_are_not_reported_as_healthy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            route = root / "net/route"
            route.parent.mkdir()
            route.write_text("Iface Destination Gateway Flags\n"
                             "eth0 00000000 010200C0 0003\n")
            (root / "vmstat").write_text("pgfault 12\npswpout 4\nnot_selected 8\n")
            (root / "stat").write_text("cpu 1 2 3\nprocs_blocked 2\nbtime 4\n")
            self.assertEqual(OBSERVER.gateway(root, "eth0"), "192.0.2.1")
            with patch.object(OBSERVER, "ping", return_value={"returncode": 1}):
                record = OBSERVER.snapshot("eth0", proc=root, sysfs=root)
            self.assertEqual(record["gateway"], {"returncode": 1})
            self.assertEqual(record["link"]["carrier"], {"errno": 2})
            self.assertEqual(record["statistics"], {})
            self.assertEqual(record["proc"]["vmstat"], "pgfault 12\npswpout 4")
            self.assertIn("procs_blocked 2", record["proc"]["stat"])
            with patch.object(OBSERVER, "ping") as probe:
                record = OBSERVER.snapshot("missing", proc=root, sysfs=root)
            probe.assert_not_called()
            self.assertEqual(record["gateway"], {"error": "ValueError"})

    def test_storage_exhaustion_preserves_reserved_and_inode_counts(self):
        class Full:
            f_bavail = 0
            f_bfree = 2
            f_frsize = 4096
            f_blocks = 100
            f_favail = 70
        with patch.object(os, "statvfs", return_value=Full()):
            self.assertEqual(OBSERVER.space(Path("/disk")), {
                "bytes_available": 0, "bytes_free_including_reserved": 8192,
                "bytes_total": 409600, "inodes_available": 70,
            })
        with patch.object(os, "statvfs", side_effect=FileNotFoundError(2, "missing")):
            self.assertEqual(OBSERVER.space(Path("/missing")), {"errno": 2})

    def test_ping_timeout_and_missing_binary_are_errors(self):
        for error in (FileNotFoundError(), subprocess.TimeoutExpired("ping", 2)):
            with self.subTest(error=type(error).__name__):
                with patch.object(subprocess, "run", side_effect=error):
                    self.assertEqual(OBSERVER.ping("192.0.2.1"),
                                     {"error": type(error).__name__})

    def test_sigterm_stops_a_long_sampling_interval(self):
        import signal
        import time
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.jsonl"
            interface = os.listdir("/sys/class/net")[0]
            process = subprocess.Popen([
                "python3", str(SPEC.origin), "--interface", interface,
                "--seconds", "30", "--interval", "30", "--log", str(path),
            ])
            try:
                deadline = time.monotonic() + 5
                while not path.exists() and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertTrue(path.exists())
                process.send_signal(signal.SIGTERM)
                self.assertEqual(process.wait(timeout=3), 0)
                self.assertEqual(len(path.read_text().splitlines()), 1)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
