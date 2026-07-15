import base64
import hashlib
import html
import importlib.util
import json
from pathlib import Path
import re
import shutil
import tempfile
import textwrap
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "Tools/firmware_release/build_release.py"
DRAFT_SCRIPT = REPO_ROOT / "Tools/firmware_release/create_github_draft.sh"

SPEC = importlib.util.spec_from_file_location("wingie2_firmware_release", SCRIPT)
RELEASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELEASE)


class FirmwareReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.build_dir = self.root / "build"
        self.build_dir.mkdir()
        (self.build_dir / "Wingie2.ino.bootloader.bin").write_bytes(b"BOOTLOADER")
        (self.build_dir / "Wingie2.ino.partitions.bin").write_bytes(
            b"VALID_PARTITIONS"
        )
        (self.build_dir / "Wingie2.ino.bin").write_bytes(b"APP")

        self.boot_app0 = self.root / "boot_app0.bin"
        self.boot_app0.write_bytes(b"FIXED_BOOT_APP0")
        self.original_boot_app0_hash = RELEASE.BOOT_APP0_SHA256
        self.original_esptool_bundle_hash = RELEASE.ESPTOOL_BUNDLE_SHA256
        self.original_md5_script_hash = RELEASE.MD5_SCRIPT_SHA256
        RELEASE.BOOT_APP0_SHA256 = self.sha256(self.boot_app0)

        self.flasher_page = self.root / "wingie_flasher.html"
        self.flasher_page.write_text(
            "<!doctype html><title>Wingie2</title>\n"
            f"{RELEASE.STANDALONE_MARKER}\n"
            f"<pre>{RELEASE.STANDALONE_LICENSE_MARKER}</pre>\n"
            "<script>window.pageLoaded = true;</script>\n"
        )
        self.esptool_bundle = self.root / "esptool-js.bundle.js"
        self.esptool_bundle.write_text(
            "class EmbeddedTransport {}\n"
            "class EmbeddedLoader {}\n"
            "export {EmbeddedTransport as Transport, EmbeddedLoader as ESPLoader};\n"
        )
        self.md5_script = self.root / "md5.min.js"
        self.md5_script.write_text("window.md5 = value => String(value.length);")
        RELEASE.ESPTOOL_BUNDLE_SHA256 = self.sha256(self.esptool_bundle)
        RELEASE.MD5_SCRIPT_SHA256 = self.sha256(self.md5_script)

        self.esptool = self.root / "fake_esptool.py"
        self.esptool.write_text(
            textwrap.dedent(
                """\
                from pathlib import Path
                import sys

                image = Path(sys.argv[-1])
                if image.read_bytes().startswith(b"CORRUPT"):
                    print("Checksum: 00 (invalid)")
                    raise SystemExit(2)
                print("Image version: 1")
                print("Checksum: ab (valid)")
                if image.name == "Wingie2.ino.bin":
                    print("Validation Hash: " + "1" * 64 + " (valid)")
                """
            ),
            encoding="utf-8",
        )

        self.gen_esp32part = self.root / "fake_gen_esp32part.py"
        self.gen_esp32part.write_text(
            textwrap.dedent(
                """\
                from pathlib import Path
                import sys

                image = Path(sys.argv[-1]).read_bytes()
                print("# ESP-IDF Partition Table")
                print("# Name, Type, SubType, Offset, Size, Flags")
                print("nvs,data,nvs,0x9000,20K,")
                print("otadata,data,ota,0xe000,8K,")
                if image == b"VALID_PARTITIONS":
                    print("app0,app,ota_0,0x10000,1280K,")
                else:
                    print("app0,app,ota_0,0x10000,1279K,")
                print("app1,app,ota_1,0x150000,1280K,")
                print("spiffs,data,spiffs,0x290000,1472K,")
                """
            ),
            encoding="utf-8",
        )
        self.output_root = self.root / "dist"

    def tearDown(self):
        RELEASE.BOOT_APP0_SHA256 = self.original_boot_app0_hash
        RELEASE.ESPTOOL_BUNDLE_SHA256 = self.original_esptool_bundle_hash
        RELEASE.MD5_SCRIPT_SHA256 = self.original_md5_script_hash
        self.temporary.cleanup()

    @staticmethod
    def sha256(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def inputs(self, version="9.8.7-test"):
        return RELEASE.ReleaseInputs(
            build_dir=self.build_dir,
            boot_app0=self.boot_app0,
            version=version,
            esptool_bundle=self.esptool_bundle,
            md5_script=self.md5_script,
            flasher_page=self.flasher_page,
            esptool=self.esptool,
            gen_esp32part=self.gen_esp32part,
            output_root=self.output_root,
        )

    def test_manifest_has_exact_layout_hashes_and_no_full_erase(self):
        package_dir = RELEASE.build_release(self.inputs())
        manifest = json.loads((package_dir / "manifest.json").read_text())

        self.assertEqual(
            set(manifest),
            {
                "schema",
                "name",
                "version",
                "chipFamily",
                "esptoolJs",
                "flash",
                "preserve",
                "parts",
            },
        )
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["chipFamily"], "ESP32")
        self.assertEqual(manifest["esptoolJs"], "0.6.0")
        self.assertIs(manifest["flash"]["eraseAll"], False)
        self.assertEqual(
            manifest["preserve"],
            [{"name": "nvs", "offset": 0x9000, "size": 0x5000}],
        )
        self.assertEqual(
            [part["offset"] for part in manifest["parts"]],
            [0x1000, 0x8000, 0xE000, 0x10000],
        )
        self.assertEqual(
            [part["name"] for part in manifest["parts"]],
            ["bootloader", "partitions", "boot_app0", "app"],
        )
        for part in manifest["parts"]:
            artifact = package_dir / part["path"]
            self.assertTrue(artifact.is_file())
            self.assertIn(manifest["version"], artifact.name)
            self.assertEqual(part["size"], artifact.stat().st_size)
            self.assertEqual(part["sha256"], self.sha256(artifact))
            self.assertFalse(
                RELEASE.ranges_overlap(
                    part["offset"], RELEASE.flash_span_size(part["size"]), 0x9000, 0x5000
                )
            )

    def test_flash_ranges_are_checked_at_4k_sector_granularity(self):
        self.assertEqual(RELEASE.flash_span_size(1), 0x1000)
        self.assertEqual(RELEASE.flash_span_size(0x1000), 0x1000)
        self.assertEqual(RELEASE.flash_span_size(0x1001), 0x2000)

        image = self.root / "unaligned.bin"
        image.write_bytes(b"X")
        part = RELEASE.FlashPart("unaligned", image, image.name, 0x8800, 0x1000)
        with self.assertRaisesRegex(RELEASE.ReleaseError, "4 KiB flash sector"):
            RELEASE.validate_flash_parts((part,))

        image.write_bytes(b"X" * 0x1001)
        part = RELEASE.FlashPart("partitions", image, image.name, 0x8000, 0x2000)
        with self.assertRaisesRegex(RELEASE.ReleaseError, "0x9000 NVS"):
            RELEASE.validate_flash_parts((part,))

    def test_sha256sums_covers_every_other_release_file(self):
        package_dir = RELEASE.build_release(self.inputs())
        checksum_lines = (package_dir / "SHA256SUMS.txt").read_text().splitlines()
        checksums = dict(line.split("  ", 1)[::-1] for line in checksum_lines)
        expected_files = {
            path.relative_to(package_dir).as_posix()
            for path in package_dir.rglob("*")
            if path.is_file() and path.name != "SHA256SUMS.txt"
        }

        self.assertEqual(set(checksums), expected_files)
        for relative_path, expected_hash in checksums.items():
            self.assertEqual(self.sha256(package_dir / relative_path), expected_hash)

    def test_standalone_html_embeds_manifest_images_and_browser_dependencies(self):
        package_dir = RELEASE.build_release(self.inputs("standalone"))
        standalone = package_dir / "Wingie2-standalone.standalone.html"
        source = standalone.read_text(encoding="utf-8")
        self.assertNotIn(RELEASE.STANDALONE_MARKER, source)
        self.assertNotIn(RELEASE.STANDALONE_LICENSE_MARKER, source)
        self.assertIn("window.__WINGIE_EMBEDDED_RELEASE__", source)
        self.assertIn("window.__WINGIE_ESPTOOL_READY__", source)
        self.assertIn('type="module" onerror=', source)
        self.assertIn("EmbeddedTransport", source)
        self.assertIn("window.md5", source)
        self.assertIn("Wingie2 web flasher third-party software notices", source)
        aggregate = (package_dir / "THIRD_PARTY_LICENSES.txt").read_text()
        self.assertIn(html.escape(aggregate), source)

        match = re.search(
            r"window\.__WINGIE_EMBEDDED_RELEASE__ = (?P<payload>\{.*\});\n"
            r"\s*window\.__WINGIE_ESPTOOL_READY__",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        payload = json.loads(match.group("payload"))
        self.assertEqual(payload["manifest"], json.loads((package_dir / "manifest.json").read_text()))
        self.assertEqual(
            sorted(payload["images"]),
            ["app", "boot_app0", "bootloader", "partitions"],
        )
        for part in payload["manifest"]["parts"]:
            self.assertEqual(
                base64.b64decode(payload["images"][part["name"]]),
                (package_dir / part["path"]).read_bytes(),
            )

    def test_third_party_licenses_are_pinned_packaged_and_checksummed(self):
        package_dir = RELEASE.build_release(self.inputs("licenses"))
        aggregate = (package_dir / "THIRD_PARTY_LICENSES.txt").read_text()
        checksum_source = (package_dir / "SHA256SUMS.txt").read_text()

        for component, license_name, filename, expected_hash in RELEASE.LICENSE_FILES:
            source = RELEASE.LICENSE_DIRECTORY / filename
            packaged = package_dir / "licenses" / filename
            self.assertEqual(self.sha256(source), expected_hash)
            self.assertEqual(packaged.read_bytes(), source.read_bytes())
            self.assertIn(component, aggregate)
            self.assertIn(f"License: {license_name}", aggregate)
            self.assertIn(f"licenses/{filename}", checksum_source)
        self.assertIn("THIRD_PARTY_LICENSES.txt", checksum_source)

    def test_missing_third_party_license_fails_closed(self):
        missing_license_dir = self.root / "missing-licenses"
        missing_license_dir.mkdir()
        with mock.patch.object(RELEASE, "LICENSE_DIRECTORY", missing_license_dir):
            with self.assertRaisesRegex(RELEASE.ReleaseError, "license 不存在"):
                RELEASE.build_release(self.inputs("missing-license"))
        self.assertFalse(
            (self.output_root / "wingie2-firmware-missing-license").exists()
        )

    def test_substituted_third_party_license_fails_closed(self):
        license_dir = self.root / "substituted-licenses"
        shutil.copytree(RELEASE.LICENSE_DIRECTORY, license_dir)
        filename = RELEASE.LICENSE_FILES[0][2]
        (license_dir / filename).write_text("substituted license", encoding="utf-8")
        with mock.patch.object(RELEASE, "LICENSE_DIRECTORY", license_dir):
            with self.assertRaisesRegex(RELEASE.ReleaseError, "license SHA256"):
                RELEASE.build_release(self.inputs("substituted-license"))
        self.assertFalse(
            (self.output_root / "wingie2-firmware-substituted-license").exists()
        )

    def test_standalone_rejects_vendor_without_required_module_exports(self):
        self.esptool_bundle.write_text("export {Missing as SomethingElse};")
        RELEASE.ESPTOOL_BUNDLE_SHA256 = self.sha256(self.esptool_bundle)
        with self.assertRaisesRegex(RELEASE.ReleaseError, "缺少 Transport export"):
            RELEASE.build_release(self.inputs("missing-export"))
        self.assertFalse(
            (self.output_root / "wingie2-firmware-missing-export").exists()
        )

    def test_app_at_partition_boundary_is_allowed(self):
        (self.build_dir / "Wingie2.ino.bin").write_bytes(
            b"A" * RELEASE.APP_MAX_SIZE
        )
        package_dir = RELEASE.build_release(self.inputs("boundary"))
        app = next(
            part
            for part in json.loads((package_dir / "manifest.json").read_text())[
                "parts"
            ]
            if part["name"] == "app"
        )
        self.assertEqual(app["size"], 0x140000)

    def test_app_over_partition_boundary_is_rejected_without_partial_package(self):
        (self.build_dir / "Wingie2.ino.bin").write_bytes(
            b"A" * (RELEASE.APP_MAX_SIZE + 1)
        )
        with self.assertRaisesRegex(RELEASE.ReleaseError, "超过 0x140000"):
            RELEASE.build_release(self.inputs("oversize"))
        self.assertFalse((self.output_root / "wingie2-firmware-oversize").exists())

    def test_corrupt_app_image_is_rejected_by_image_info(self):
        (self.build_dir / "Wingie2.ino.bin").write_bytes(b"CORRUPT_APP")
        with self.assertRaisesRegex(RELEASE.ReleaseError, "app image_info 校验失败"):
            RELEASE.build_release(self.inputs("corrupt"))
        self.assertFalse((self.output_root / "wingie2-firmware-corrupt").exists())

    def test_wrong_partition_layout_is_rejected(self):
        (self.build_dir / "Wingie2.ino.partitions.bin").write_bytes(
            b"WRONG_PARTITIONS"
        )
        with self.assertRaisesRegex(RELEASE.ReleaseError, "精确布局"):
            RELEASE.build_release(self.inputs("wrong-layout"))

    def test_boot_app0_must_match_fixed_core_hash(self):
        self.boot_app0.write_bytes(b"ANOTHER_BOOT_APP0")
        with self.assertRaisesRegex(RELEASE.ReleaseError, "boot_app0 SHA256"):
            RELEASE.build_release(self.inputs("wrong-boot-app0"))

    def test_boot_app0_hash_is_pinned_to_core_204_cn(self):
        self.assertEqual(
            self.original_boot_app0_hash,
            "f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0",
        )

    def test_missing_vendor_dependency_fails_closed(self):
        self.esptool_bundle.unlink()
        with self.assertRaisesRegex(RELEASE.ReleaseError, "esptool-js bundle"):
            RELEASE.build_release(self.inputs("missing-vendor"))

    def test_substituted_vendor_dependency_fails_closed(self):
        self.esptool_bundle.write_text("// substituted")
        with self.assertRaisesRegex(RELEASE.ReleaseError, "bundle SHA256"):
            RELEASE.build_release(self.inputs("substituted-vendor"))

    def test_vendor_hashes_are_pinned_to_reviewed_builds(self):
        self.assertEqual(
            self.original_esptool_bundle_hash,
            "7c361337d5bba7271cb0d9741f165a3b87137ff9284c13f112a6e197c48cd0da",
        )
        self.assertEqual(
            self.original_md5_script_hash,
            "6164d009d3fcf65edd5c47c4b76a0d0580dea4bce929eec89bec744fdec10e15",
        )

    def test_draft_helper_can_only_create_a_draft(self):
        source = DRAFT_SCRIPT.read_text(encoding="utf-8")
        self.assertEqual(source.count("gh release create"), 1)
        self.assertIn("--draft", source)
        self.assertNotIn("gh release edit", source)
        self.assertNotIn("gh release upload", source)
        self.assertNotIn("git push", source)
        self.assertIn('standalone="Wingie2-$version.standalone.html"', source)
        self.assertIn('"$release_dir/$standalone"', source)
        self.assertIn("THIRD_PARTY_LICENSES.txt", source)
        self.assertIn("cmp -s", source)


if __name__ == "__main__":
    unittest.main()
