#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/tmp_stardust_demo"
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
PHASH_HEX="${PHASH_HEX:-00112233445566778899aabbccddeeff0011}"
SEED="${SEED:-1}"
STRENGTH="${STRENGTH:-6}"
SP_DENSITY="${SP_DENSITY:-60}"
P_DENSITY="${P_DENSITY:-60}"
BIT_PROFILE="${BIT_PROFILE:-184}"

echo "== Stardust image stego demo =="
echo "root: $ROOT"
echo "workdir: $WORK"

mkdir -p "$WORK" "$WORK/aligned"

echo "== Building unified-api BLS-BN158 payload tool =="
make -C "$ROOT/unified-api" stego_payload_tool SCHEME=bls-bn158 >/dev/null

echo "== Patching Stardust for local integration =="
python3 "$ROOT/scripts/patch_stardust.py"

echo "== Building Stardust tools =="
git -C "$ROOT/stardust" submodule sync >/dev/null 2>&1 || true
git -C "$ROOT/stardust" submodule update --init --recursive >/dev/null 2>&1 || true
cmake -S "$ROOT/stardust" -B "$ROOT/stardust/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSD_WITH_OPENCV=ON \
  -DOpenCV_DIR=/usr/lib/x86_64-linux-gnu/cmake/opencv4 \
  -DSD_BUILD_OPENCV=OFF \
  -DSD_BUILD_OPENCV_STATIC=OFF \
  -DSD_STATIC_BINARIES=OFF >/dev/null
CPLUS_INCLUDE_PATH=/usr/include/opencv4 cmake --build "$ROOT/stardust/build" -j4 --target sffw-embed align extract >/dev/null

echo "== Generating BLS-BN158 payload (salt || sig only) =="
"$ROOT/unified-api/stego_payload_tool" generate "$PHASH_HEX" \
  "$WORK/payload.bin" "$WORK/pk.bin" "$WORK/phash.bin" | tee "$WORK/generate.txt"

PAYLOAD_HEX="$(grep '^payload_hex=' "$WORK/generate.txt" | cut -d= -f2)"
PAYLOAD_BYTES="$(grep '^payload_bytes=' "$WORK/generate.txt" | cut -d= -f2)"
echo "payload_hex: $PAYLOAD_HEX"
echo "payload_bytes: $PAYLOAD_BYTES"

echo "== Generating cover frame =="
ffmpeg -y -f lavfi -i "testsrc=size=${WIDTH}x${HEIGHT}:rate=1" -frames:v 1 -pix_fmt yuv420p -f rawvideo "$WORK/cover.yuv" >/dev/null 2>&1

echo "== Embedding payload into image with Stardust =="
"$ROOT/stardust/build/embed/sffw-embed" \
  --input-file "$WORK/cover.yuv" \
  --output-file "$WORK/embedded.yuv" \
  --strength "$STRENGTH" \
  --sp-density "$SP_DENSITY" \
  --p-density "$P_DENSITY" \
  --bit-profile "$BIT_PROFILE" \
  --wm-id "$PAYLOAD_HEX" \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  --pix-fmt yuv420p \
  --seed "$SEED" \
  --fec 2

echo "== Preparing files for alignment and extraction =="
ffmpeg -y -f rawvideo -pix_fmt yuv420p -s ${WIDTH}x${HEIGHT} -i "$WORK/embedded.yuv" -frames:v 1 "$WORK/embedded.png" >/dev/null 2>&1
ffmpeg -y -f rawvideo -pix_fmt yuv420p -s ${WIDTH}x${HEIGHT} -i "$WORK/cover.yuv" -frames:v 1 -pix_fmt yuv444p -f rawvideo "$WORK/reference444.yuv" >/dev/null 2>&1
python3 - <<PY
from pathlib import Path
root=Path(r"$WORK")
W=$WIDTH; H=$HEIGHT
(root/'reference.y').write_bytes((root/'cover.yuv').read_bytes()[:W*H])
PY

echo "== Aligning the embedded image =="
rm -rf "$WORK/aligned"
mkdir -p "$WORK/aligned"
"$ROOT/stardust/build/align/align" \
  --input-file "$WORK/embedded.png" \
  --reference-file "$WORK/reference444.yuv" \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  --result-folder "$WORK/aligned" \
  --output 0

echo "== Extracting watermark from aligned image =="
"$ROOT/stardust/build/extract/extract" \
  --reference-file "$WORK/reference.y" \
  --input-folder "$WORK/aligned" \
  --strength "$STRENGTH" \
  --sp-density "$SP_DENSITY" \
  --bit-profile "$BIT_PROFILE" \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  --seed "$SEED" \
  --fec 2 | tee "$WORK/extract.txt"

EXTRACTED_HEX="$(grep '^WM ID Hex:' "$WORK/extract.txt" | awk -F': ' '{print tolower($2)}')"
echo "$EXTRACTED_HEX" | xxd -r -p > "$WORK/extracted_payload.bin"

echo "== Verifying extracted payload with unified BLS flow =="
"$ROOT/unified-api/stego_payload_tool" verify \
  "$WORK/extracted_payload.bin" "$WORK/pk.bin" "$WORK/phash.bin" | tee "$WORK/verify.txt"

echo
echo "== Summary =="
echo "Original payload hex : $PAYLOAD_HEX"
echo "Extracted payload hex: $EXTRACTED_HEX"
if [ "$PAYLOAD_HEX" = "$EXTRACTED_HEX" ]; then
  echo "Payload round-trip   : OK"
else
  echo "Payload round-trip   : MISMATCH"
  exit 1
fi
grep '^verify_status=' "$WORK/verify.txt" | cut -d= -f2 | sed 's/^/BLS verification     : /'
echo "Artifacts in         : $WORK"
