# Wingie2 firmware release tools

`build_release.py` validates the four flash images, the exact 4 MB partition layout, the fixed Arduino Core 2.0.4-cn `boot_app0.bin`, and every published checksum before it creates a release directory. It also generates `Wingie2-VERSION.standalone.html`, containing the manifest, all four images, `esptool-js`, and MD5 implementation in one file. It never opens a serial port and contains no erase or flash operation.

```bash
python3 Tools/firmware_release/build_release.py \
  --build-dir /private/tmp/wingie2-two-source-product-build \
  --boot-app0 /Users/mengwu/Library/Arduino15/packages/esp32/hardware/esp32/2.0.4-cn/tools/partitions/boot_app0.bin \
  --version VERSION \
  --esptool-bundle /path/to/esptool-js.bundle.js \
  --md5-script /path/to/md5.min.js
```

The two browser dependencies are mandatory explicit inputs. Their SHA-256 values are pinned to the official `esptool-js 0.6.0` bundle and `js-md5 0.8.0` minified build before they are copied to stable names under `vendor/`; a missing or substituted dependency fails closed instead of producing an online-dependent release.

The source notices for `esptool-js`, `atob-lite`, `pako` (MIT and Zlib), `tslib`, and `js-md5` are also pinned by SHA-256. Every package includes the source notice files under `licenses/`, an aggregate `THIRD_PARTY_LICENSES.txt`, and the same aggregate text inside the standalone page.

Deploy the standalone HTML as a top-level HTTPS page, preferably through GitHub Pages on a dedicated subdomain. For Squarespace, add a button that opens that URL in the current tab or a new tab. Do not paste the multi-megabyte file into a Code Block and do not iframe it; top-level navigation avoids cross-origin Web Serial permission-policy failures.

After automated validation, create a private GitHub Draft for review:

```bash
Tools/firmware_release/create_github_draft.sh VERSION
```

The helper verifies `SHA256SUMS.txt` and invokes only `gh release create --draft`. It does not push commits, publish the release, or flash hardware. Do not publish the Draft until both required hardware gates have passed.
