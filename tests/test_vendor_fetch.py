# SPDX-License-Identifier: GPL-2.0
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch

spec = importlib.util.spec_from_file_location("firmware", Path(__file__).parents[1] / "scripts/extract-firmware.py")
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)


class VendorFetchTests(unittest.TestCase):
    def test_token_fetches_same_revision_through_contents_api(self):
        url = "https://raw.githubusercontent.com/hardkernel/linux/abcdef/drivers/firmware.h"
        response = MagicMock()
        response.__enter__.return_value.read.return_value = b"pinned contents"
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-token"}), patch.object(firmware.urllib.request, "urlopen", return_value=response) as fetch:
            self.assertEqual(firmware._fetch(url), b"pinned contents")
        request = fetch.call_args.args[0]
        self.assertEqual(request.full_url, "https://api.github.com/repos/hardkernel/linux/contents/drivers/firmware.h?ref=abcdef")
        self.assertEqual(request.get_header("Authorization"), "Bearer test-token")
        self.assertEqual(request.get_header("Accept"), "application/vnd.github.raw+json")

    def test_other_hosts_do_not_receive_token(self):
        response = MagicMock()
        response.__enter__.return_value.read.return_value = b"contents"
        with patch.dict(os.environ, {"GITHUB_TOKEN": "test-token"}), patch.object(firmware.urllib.request, "urlopen", return_value=response) as fetch:
            firmware._fetch("https://example.com/archive")
        self.assertIsNone(fetch.call_args.args[0].get_header("Authorization"))


if __name__ == "__main__":
    unittest.main()
