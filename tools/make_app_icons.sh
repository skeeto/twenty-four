#!/bin/sh
# Regenerate all platform icon assets from web/icon.svg.
# Requires (macOS): rsvg-convert, iconutil, python3. Run from the repo root.
set -e
cd "$(dirname "$0")/.."

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for s in 16 32 48 64 128 256 512 1024; do
    rsvg-convert -w "$s" -h "$s" web/icon.svg -o "$tmp/$s.png"
done

# Embedded runtime window icon (decoded by stb_image at startup).
cp "$tmp/256.png" assets/icon.png

# Windows .ico (EXE + window icon) and web favicon.
python3 tools/make_ico.py assets/icon.ico \
    16:"$tmp/16.png" 32:"$tmp/32.png" 48:"$tmp/48.png" \
    64:"$tmp/64.png" 128:"$tmp/128.png" 256:"$tmp/256.png"
python3 tools/make_ico.py web/favicon.ico \
    16:"$tmp/16.png" 32:"$tmp/32.png" 48:"$tmp/48.png"

# macOS .icns.
iconset="$tmp/icon.iconset"
mkdir -p "$iconset"
cp "$tmp/16.png"   "$iconset/icon_16x16.png"
cp "$tmp/32.png"   "$iconset/icon_16x16@2x.png"
cp "$tmp/32.png"   "$iconset/icon_32x32.png"
cp "$tmp/64.png"   "$iconset/icon_32x32@2x.png"
cp "$tmp/128.png"  "$iconset/icon_128x128.png"
cp "$tmp/256.png"  "$iconset/icon_128x128@2x.png"
cp "$tmp/256.png"  "$iconset/icon_256x256.png"
cp "$tmp/512.png"  "$iconset/icon_256x256@2x.png"
cp "$tmp/512.png"  "$iconset/icon_512x512.png"
cp "$tmp/1024.png" "$iconset/icon_512x512@2x.png"
iconutil -c icns "$iconset" -o assets/icon.icns

echo "regenerated: assets/icon.png assets/icon.ico assets/icon.icns web/favicon.ico"
