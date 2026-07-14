import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "Tools/flash_mode_filter_candidate.py"


class FlashModeFilterCandidateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.port = self.root / "serial-port"
        self.port.touch()
        self.log = self.root / "esptool.jsonl"
        self.esptool = self.root / "fake_esptool.py"
        self.esptool.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                import os
                from pathlib import Path
                import sys

                args = sys.argv[1:]
                with Path(os.environ["FAKE_ESPTOOL_LOG"]).open("a") as handle:
                    handle.write(json.dumps(args) + "\\n")
                """
            ),
            encoding="utf-8",
        )
        self.esptool.chmod(0o755)

        self.original_bytes = b"\xe9original-image"
        self.known_issue_bytes = b"\xe9known-issue-image"
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "flash": {
                        "chip": "esp32",
                        "app_address": "0x10000",
                        "default_baud": 460800,
                    },
                    "candidates": [
                        self.candidate(
                            "original", "original.bin", self.original_bytes, False
                        ),
                        self.candidate(
                            "known-issue",
                            "known-issue.bin",
                            self.known_issue_bytes,
                            True,
                        ),
                        {
                            "id": "unavailable",
                            "name": "Unavailable",
                            "availability": "unavailable",
                            "artifact": None,
                            "size": None,
                            "sha256": None,
                            "test_state": "不可生成",
                            "known_issue": False,
                            "note": "no complete image",
                            "source": "test",
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )
        (self.cache / "original.bin").write_bytes(self.original_bytes)
        (self.cache / "known-issue.bin").write_bytes(self.known_issue_bytes)

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def candidate(candidate_id, artifact, content, known_issue):
        return {
            "id": candidate_id,
            "name": candidate_id.title(),
            "availability": "available",
            "artifact": artifact,
            "size": len(content),
            "sha256": hashlib.sha256(content).hexdigest(),
            "test_state": "test",
            "known_issue": known_issue,
            "note": "known issue" if known_issue else "normal",
            "source": "test",
        }

    def run_cli(self, *arguments):
        environment = os.environ.copy()
        environment["FAKE_ESPTOOL_LOG"] = str(self.log)
        command = [
            sys.executable,
            str(SCRIPT),
            "--manifest",
            str(self.manifest),
            "--cache-dir",
            str(self.cache),
            "--esptool",
            str(self.esptool),
            *arguments,
        ]
        return subprocess.run(command, text=True, capture_output=True, env=environment)

    def logged_commands(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_list_distinguishes_ready_attention_and_unavailable(self):
        result = self.run_cli("list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("original\t就绪", result.stdout)
        self.assertIn("known-issue\t就绪/注意", result.stdout)
        self.assertIn("unavailable\t不可用", result.stdout)

    def test_repository_original_is_tap_sequencer_fixed_image(self):
        manifest = json.loads(
            (REPO_ROOT / "Tools/mode_filter_candidates.json").read_text()
        )
        original = next(
            candidate
            for candidate in manifest["candidates"]
            if candidate["id"] == "original"
        )
        self.assertEqual(original["artifact"], "original-tap-sequencer-fixed.bin")
        self.assertEqual(original["size"], 1_189_744)
        self.assertEqual(
            original["sha256"],
            "7d02277ac41c6f6c7825c53d9a2e118c92c4590c4a191c668f96c23c62f09d3b",
        )
        self.assertIn("2e049bf", original["source"])

    def test_flash_uses_one_write_session_and_no_separate_verify(self):
        result = self.run_cli(
            "flash",
            "original",
            "--port",
            str(self.port),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.logged_commands()
        operations = [
            next(
                operation
                for operation in ("image_info", "write_flash", "verify_flash")
                if operation in command
            )
            for command in commands
        ]
        self.assertEqual(
            operations,
            ["image_info", "write_flash"],
        )
        self.assertTrue(all("read_flash" not in command for command in commands))
        self.assertTrue(all("verify_flash" not in command for command in commands))
        write = commands[1]
        self.assertEqual(write.count("hard_reset"), 1)
        self.assertEqual(write[write.index("--after") + 1], "hard_reset")
        write_index = write.index("write_flash")
        self.assertEqual(write[write_index + 1 : write_index + 3], ["-z", "0x10000"])

    def test_bad_hash_refuses_before_running_esptool(self):
        (self.cache / "original.bin").write_bytes(b"tampered-data")
        result = self.run_cli(
            "flash",
            "original",
            "--port",
            str(self.port),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())

    def test_known_issue_candidate_needs_no_extra_override(self):
        result = self.run_cli(
            "flash",
            "known-issue",
            "--port",
            str(self.port),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("known issue", result.stdout)
        self.assertNotIn("--allow-risky", result.stdout)

    def test_unavailable_candidate_is_never_flashable(self):
        result = self.run_cli(
            "flash",
            "unavailable",
            "--port",
            str(self.port),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("没有可烧录镜像", result.stderr)
        self.assertFalse(self.log.exists())

    def test_dry_run_does_not_invoke_esptool(self):
        result = self.run_cli(
            "--dry-run",
            "flash",
            "original",
            "--port",
            str(self.port),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("write_flash -z 0x10000", result.stdout)
        self.assertNotIn("read_flash", result.stdout)
        self.assertNotIn("verify_flash", result.stdout)
        self.assertIn("演练完成：未读取或写入设备", result.stdout)
        self.assertNotIn("烧录完成", result.stdout)
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()
