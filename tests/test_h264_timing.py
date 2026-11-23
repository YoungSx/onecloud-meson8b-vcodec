"""Replay actual H.264 IRQ, timestamp, pool and LAST/EOS functions on a host.

Only kernel services, MMIO, firmware pictures and byte conversion are faked.
This cannot reproduce firmware behaviour or establish the historical 299/300
failure's cause. See docs/h264-regression.md for the boundary and replay log.
"""

import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def section(path, start, end):
    source = (ROOT / path).read_text()
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def driver_code():
    # Keep the production bodies intact, including timestamp-list consumption.
    return "\n".join([
        "\n".join(re.findall(r"^#define (?:PIC_|BUF_IDX_|OFFSET_|ERROR_)\w+[^\n]*",
                             (ROOT / "vdec/codec_h264.c").read_text(), re.MULTILINE)),
        section("vdec/vdec_helpers.c", "static void amvdec_mark_picture(",
                "static void dst_buf_done("),
        section("vdec/vdec_helpers.c", "bool amvdec_timestamp_for_offset(",
                "void amvdec_dst_buf_done_offset("),
        section("vdec/vdec_helpers.c", "void amvdec_pool_picture_done(",
                "/* Return every still-queued capture buffer"),
        section("vdec/vdec_helpers.c", "void amvdec_src_change(",
                "void amvdec_abort("),
        section("vdec/meson8b-capture.c", "static void amvdec_pool_end_locked(",
                "void amvdec_pool_free("),
        section("vdec/codec_h264.c", "static u32 get_offset_msb(",
                "static irqreturn_t codec_h264_threaded_isr("),
        section("vdec/vdec.c", "static int vdec_wait_inactive(",
                "static void vdec_poweroff("),
        section("vdec/vdec.c", "static void vdec_vb2_buf_finish(",
                "static const struct vb2_ops"),
        section("vdec/vdec.c", "static irqreturn_t vdec_isr(",
                "static const struct of_device_id"),
    ])


class H264TimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.directory = pathlib.Path(cls.temp.name)
        cls.code = driver_code()
        cls.binary = cls.compile(cls.code, "current")
        cls.log = ROOT / "build/host-tests/h264-timing.log"
        cls.log.parent.mkdir(parents=True, exist_ok=True)
        cls.log.write_text("Synthetic firmware pictures; actual driver control flow.\n")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def compile(cls, code, name):
        (cls.directory / "driver.inc").write_text(code)
        binary = cls.directory / name
        subprocess.run([
            "cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
            "-Wno-sign-compare",
            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
            "-I", str(cls.directory), "-I", str(ROOT / "vdec"),
            str(ROOT / "tests/h264-timing.c"), "-o", str(binary),
        ], check=True)
        return binary

    def replay(self, *args):
        result = subprocess.run([str(self.binary), *map(str, args)],
                                capture_output=True, text=True, timeout=10)
        with self.log.open("a") as log:
            log.write(f"REPLAY {' '.join(map(str, args))}\n" + result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_300_pictures_b_frame_tail_and_capture_backpressure(self):
        for capture in (1, 4, 10):
            for batch in (1, 3, 6):
                for seed in range(1, 9):
                    with self.subTest(capture=capture, batch=batch, seed=seed):
                        self.replay("stream", capture, batch, seed)

    def test_irq_arrives_while_another_slot_is_copied(self):
        self.replay("copy-irq")

    def test_stop_waits_for_thread_completion_then_quiet_window(self):
        self.replay("wait")

    def test_source_change_last_is_not_eos_and_keeps_queued_geometry(self):
        self.replay("source-change")

    def test_cancel_discards_pending_delivery_and_reopen_has_no_stale_eos(self):
        self.replay("cancel")

    def test_fault_injections_are_detected(self):
        faults = [
            ("ignore-active-irq", "READ_ONCE(sess->irq_active) ||", "", "wait"),
            ("ignore-irq-completion-time",
             "/* Conversion in the threaded handler can outlast the inactivity window. */\n"
             "\tWRITE_ONCE(sess->last_irq_jiffies, get_jiffies_64());",
             "", "wait"),
            ("no-pts-sort", "&& sess->should_stop) {", "&& false) {", "stream"),
            ("early-eos", "v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);",
             "if (pic->eos) { struct v4l2_event e = { .type = V4L2_EVENT_EOS }; "
             "v4l2_event_queue_fh(&sess->fh, &e); }\n"
             "v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);", "stream"),
        ]
        for name, old, new, scenario in faults:
            with self.subTest(fault=name):
                self.assertEqual(self.code.count(old), 1)
                binary = self.compile(self.code.replace(old, new), name)
                result = subprocess.run([str(binary), scenario, "1", "6", "1"],
                                        capture_output=True, text=True, timeout=10)
                self.assertLess(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("Assertion", result.stderr)
                with self.log.open("a") as log:
                    log.write(f"DETECTED {name}\n{result.stderr}")
