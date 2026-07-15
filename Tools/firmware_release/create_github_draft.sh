#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 VERSION" >&2
  exit 2
fi

version=$1
repository_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
release_dir="$repository_root/dist/wingie2-firmware-$version"

if [ ! -d "$release_dir" ]; then
  echo "Release directory not found: $release_dir" >&2
  exit 1
fi

for required in manifest.json SHA256SUMS.txt README.zh-CN.md README.en.md wingie_flasher.html vendor/esptool-js.bundle.js vendor/md5.min.js; do
  if [ ! -f "$release_dir/$required" ]; then
    echo "Release artifact missing: $release_dir/$required" >&2
    exit 1
  fi
done

if ! command -v gh >/dev/null 2>&1; then
  echo "GitHub CLI (gh) is required." >&2
  exit 1
fi
if ! command -v zip >/dev/null 2>&1; then
  echo "zip is required." >&2
  exit 1
fi

(
  cd "$release_dir"
  shasum -a 256 -c SHA256SUMS.txt
)

temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/wingie2-release.XXXXXX")
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM
archive="$temporary_dir/wingie2-firmware-$version.zip"

(
  cd "$(dirname -- "$release_dir")"
  zip -qr "$archive" "$(basename -- "$release_dir")"
)

case "$version" in
  v*) tag=$version ;;
  *) tag="v$version" ;;
esac

gh release create "$tag" \
  --draft \
  --title "Wingie2 $version" \
  --target "$(git -C "$repository_root" rev-parse HEAD)" \
  --notes-file "$release_dir/README.zh-CN.md" \
  "$archive" \
  "$release_dir/SHA256SUMS.txt" \
  "$release_dir/manifest.json"
