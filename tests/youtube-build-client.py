"""Release credential validation uses synthetic application data only."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "packaging/configure-youtube-client.py"


class YouTubeBuildClient(unittest.TestCase):
    def generate(self, value, required=True):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "client.hpp"
            command = ["python3", str(SCRIPT), str(output)]
            if required:
                command.append("--required")
            result = subprocess.run(command, env={**os.environ, "CHERRIES_YOUTUBE_CLIENT_JSON": value},
                                    text=True, capture_output=True)
            return result, output.read_text() if output.exists() else None

    def test_release_refuses_missing_credentials(self):
        result, header = self.generate("")
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(header)
        self.assertIn("CHERRIES_YOUTUBE_CLIENT_JSON", result.stderr)

    def test_custom_build_can_use_import(self):
        result, header = self.generate("", required=False)
        self.assertEqual(result.returncode, 0)
        self.assertIn('CherriesBundledYouTubeId = "";', header)

    def test_rejects_other_client_types(self):
        for kind in ("web", "service_account"):
            result, header = self.generate(json.dumps({kind: {"client_id": "example.apps.googleusercontent.com", "client_secret": "synthetic"}}))
            self.assertNotEqual(result.returncode, 0)
            self.assertIsNone(header)

    def test_bad_configuration_never_echoes_credentials(self):
        marker = "synthetic-private-marker"
        for value in (marker, json.dumps({"installed": {"client_id": marker, "client_secret": marker}})):
            result, header = self.generate(value)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn(marker, result.stdout + result.stderr)
            self.assertIsNone(header)

    def test_generated_strings_compile_without_code_injection(self):
        client_id = "123-example.apps.googleusercontent.com"
        secret = 'synthetic-"\\-secret'
        result, header = self.generate(json.dumps({"installed": {"client_id": client_id, "client_secret": secret}}))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout + result.stderr, "")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "client.hpp").write_text(header)
            (root / "check.cpp").write_text('#include "client.hpp"\n#include <string_view>\n' +
                f'static_assert(std::string_view(CherriesBundledYouTubeId) == {json.dumps(client_id)});\n' +
                f'static_assert(std::string_view(CherriesBundledYouTubeSecret) == {json.dumps(secret)});\n')
            subprocess.run(["g++", "-std=c++17", "-fsyntax-only", str(root / "check.cpp")], check=True)


if __name__ == "__main__":
    unittest.main()
