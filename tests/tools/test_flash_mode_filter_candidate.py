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
BACKUP_SIZE = 0x140000


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
                if "read_flash" in args:
                    index = args.index("read_flash")
                    size = int(args[index + 2], 0)
                    output = Path(args[index + 3])
                    output.write_bytes(b"\\xe9" + bytes(size - 1))
                """
            ),
            encoding="utf-8",
        )
        self.esptool.chmod(0o755)

        self.ready_bytes = b"\xe9ready-image"
        self.risky_bytes = b"\xe9risky-image"
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "flash": {
                        "chip": "esp32",
                        "app_address": "0x10000",
                        "backup_size": "0x140000",
                        "default_baud": 460800,
                    },
                    "candidates": [
                        self.candidate("ready", "ready.bin", self.ready_bytes, False),
                        self.candidate("risky", "risky.bin", self.risky_bytes, True),
                        {
                            "id": "unavailable",
                            "name": "Unavailable",
                            "availability": "unavailable",
                            "artifact": None,
                            "size": None,
                            "sha256": None,
                            "test_state": "不可生成",
                            "requires_allow_risky": False,
                            "note": "no complete image",
                            "source": "test",
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )
        (self.cache / "ready.bin").write_bytes(self.ready_bytes)
        (self.cache / "risky.bin").write_bytes(self.risky_bytes)

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def candidate(candidate_id, artifact, content, risky):
        return {
            "id": candidate_id,
            "name": candidate_id.title(),
            "availability": "available",
            "artifact": artifact,
            "size": len(content),
            "sha256": hashlib.sha256(content).hexdigest(),
            "test_state": "test",
            "requires_allow_risky": risky,
            "note": "known risk" if risky else "normal",
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

    def test_list_distinguishes_ready_risky_and_unavailable(self):
        result = self.run_cli("list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ready\t就绪", result.stdout)
        self.assertIn("risky\t就绪/风险", result.stdout)
        self.assertIn("unavailable\t不可用", result.stdout)

    def test_flash_backs_up_before_app_only_write_and_verifies(self):
        result = self.run_cli(
            "flash",
            "ready",
            "--port",
            str(self.port),
            "--confirm",
            "ready",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.logged_commands()
        operations = [
            next(
                operation
                for operation in ("image_info", "read_flash", "write_flash", "verify_flash")
                if operation in command
            )
            for command in commands
        ]
        self.assertEqual(
            operations,
            ["image_info", "read_flash", "image_info", "write_flash", "verify_flash"],
        )
        read = commands[1]
        read_index = read.index("read_flash")
        self.assertEqual(read[read_index + 1 : read_index + 3], ["0x10000", "0x140000"])
        write = commands[3]
        write_index = write.index("write_flash")
        self.assertEqual(write[write_index + 1 : write_index + 3], ["-z", "0x10000"])
        verify = commands[4]
        verify_index = verify.index("verify_flash")
        self.assertEqual(verify[verify_index + 1], "0x10000")

    def test_bad_hash_refuses_before_running_esptool(self):
        (self.cache / "ready.bin").write_bytes(b"tampered-data")
        result = self.run_cli(
            "flash",
            "ready",
            "--port",
            str(self.port),
            "--confirm",
            "ready",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())

    def test_risky_candidate_requires_explicit_override(self):
        result = self.run_cli(
            "flash",
            "risky",
            "--port",
            str(self.port),
            "--confirm",
            "risky",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--allow-risky", result.stderr)
        self.assertFalse(self.log.exists())

    def test_unavailable_candidate_is_never_flashable(self):
        result = self.run_cli(
            "flash",
            "unavailable",
            "--port",
            str(self.port),
            "--confirm",
            "unavailable",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("没有可烧录镜像", result.stderr)
        self.assertFalse(self.log.exists())

    def test_restore_backs_up_current_app_and_verifies_full_restore(self):
        restore_image = self.root / "restore.app0.bin"
        restore_image.write_bytes(b"\xe9" + bytes(BACKUP_SIZE - 1))
        result = self.run_cli(
            "restore",
            str(restore_image),
            "--port",
            str(self.port),
            "--confirm",
            "restore",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.logged_commands()
        operations = [
            next(
                operation
                for operation in ("image_info", "read_flash", "write_flash", "verify_flash")
                if operation in command
            )
            for command in commands
        ]
        self.assertEqual(
            operations,
            ["image_info", "read_flash", "image_info", "write_flash", "verify_flash"],
        )
        write = commands[3]
        write_index = write.index("write_flash")
        self.assertEqual(write[write_index + 3], str(restore_image.resolve()))
        verify = commands[4]
        verify_index = verify.index("verify_flash")
        self.assertEqual(verify[verify_index + 2], str(restore_image.resolve()))

    def test_backup_refuses_to_overwrite_existing_file(self):
        output = self.root / "existing-backup.bin"
        output.write_bytes(b"keep-me")
        result = self.run_cli(
            "backup",
            "--port",
            str(self.port),
            "--output",
            str(output),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("拒绝覆盖", result.stderr)
        self.assertEqual(output.read_bytes(), b"keep-me")
        self.assertFalse(self.log.exists())

    def test_dry_run_does_not_invoke_esptool_or_create_backup(self):
        result = self.run_cli(
            "--dry-run",
            "flash",
            "ready",
            "--port",
            str(self.port),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("read_flash 0x10000 0x140000", result.stdout)
        self.assertIn("write_flash -z 0x10000", result.stdout)
        self.assertIn("演练完成：未读取或写入设备", result.stdout)
        self.assertNotIn("烧录完成", result.stdout)
        self.assertFalse(self.log.exists())
        self.assertFalse((self.cache / "backups").exists())


if __name__ == "__main__":
    unittest.main()
