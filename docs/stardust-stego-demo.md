# Stardust Stego Channel Demo

This document describes the additional end-to-end demo flow that uses the
[`castlabs/stardust`](https://github.com/castlabs/stardust) forensic watermarking
implementation as the **stego transport layer**.

## Goal

The existing signature demos prove that we can create and verify compact
payloads such as:

- UOV signatures with message recovery
- BLS-BN158 payloads of `salt || signature` only (184 bits)

The Stardust-based flow demonstrates that we can also:

1. generate a real stego payload
2. embed that payload into an image
3. extract it again from the watermarked image
4. feed the extracted payload back into our verification code

This turns the abstract "stego channel" into a concrete working pipeline.

## Current Scope

The implemented Stardust demo currently supports:

- **BLS-BN158 only**
- **payload = `salt || signature` only**
- **no embedded pHash**
- **no embedded PK**

That choice is intentional:

- BLS-BN158 without embedded pHash produces a **23-byte / 184-bit** payload
- Stardust's watermark ID transport can carry up to **256 bits**
- UOV payloads (400/504 bits) are too large for the current Stardust WM-ID path
- BLS12-381 without embedded pHash is **408 bits**, also too large

## Why BN-P158 fits Stardust well

Stardust embeds a watermark identifier (`wm_id`) as a hexadecimal value. In the
 current repository version, the watermark ID path supports up to **256 bits**.

Our BLS-BN158 stego payload is:

```text
salt (16 bits) || signature (168 bits) = 184 bits = 23 bytes
```

That fits cleanly into the Stardust watermark ID field.

## Implemented Flow

The demo script is:

```bash
scripts/stardust_image_demo.sh
```

It performs the following steps:

1. builds the unified BLS-BN158 payload helper
2. builds Stardust (`sffw-embed`, `align`, `extract`)
3. generates a deterministic pHash and signs it with the real BLS-BN158 backend
4. writes the resulting stego payload (`salt || sig`) to `payload.bin`
5. converts that payload to hex and passes it to Stardust as `--wm-id`
6. generates a synthetic cover image (`testsrc`) in YUV format
7. embeds the payload into the image with `sffw-embed`
8. converts the watermarked frame to PNG
9. aligns the PNG with Stardust `align`
10. extracts the watermark with Stardust `extract`
11. parses the recovered `WM ID Hex`
12. converts the extracted watermark back into the original 23-byte payload
13. verifies the extracted payload with the unified BLS verification flow

## Files Added / Modified

### Added

- `scripts/stardust_image_demo.sh`
- `unified-api/stego_payload_tool.c`

### Modified

- `unified-api/Makefile`
- `scripts/patch_stardust.py`

## Local Stardust Patches

To make Stardust usable in this repo, a few compatibility patches were required.
These are applied automatically by:

```bash
python3 scripts/patch_stardust.py
```

The upstream `stardust/` submodule is kept clean; patches are applied locally at build/demo time.

### 1. Submodule URLs switched to HTTPS

Upstream `.gitmodules` uses SSH URLs for OpenCV submodules:

- `git@github.com:opencv/opencv.git`
- `git@github.com:opencv/opencv_contrib.git`

These were changed to HTTPS so submodules can be initialized in unattended or
non-SSH environments.

### 2. System OpenCV support fix

Upstream `align/CMakeLists.txt` and `extract/CMakeLists.txt` only called
`find_package(OpenCV ...)` when `SD_BUILD_OPENCV=ON`.

That breaks the documented "use system OpenCV" workflow with:

```bash
-DSD_BUILD_OPENCV=OFF
```

We patched both files so `find_package(OpenCV ...)` runs whenever:

```bash
SD_WITH_OPENCV=ON
```

This makes Stardust work with distro-provided OpenCV (`libopencv-dev`).

### 3. Large WM-ID extraction overflow fix

Upstream extraction converts the recovered binary watermark ID to decimal using:

```cpp
std::stoull(..., 2)
```

That crashes for watermark IDs larger than 64 bits. Our BLS payload is 184 bits,
so extraction succeeded but the CLI crashed while printing the decimal form.

We patch `stardust/extract/extract.cpp` so that large IDs no longer crash and
print:

```text
WM ID: <overflow>
```

The important field for us is still present and correct:

```text
WM ID Hex: <payload-hex>
```

## Unified Payload Helper

The helper binary built from `unified-api/stego_payload_tool.c` has two modes:

### Generate

```bash
unified-api/stego_payload_tool generate <phash_hex> <payload.bin> <pk.bin> <phash.bin>
```

This:

- initializes the selected unified backend (`SCHEME=bls-bn158`)
- generates a real BLS-BN158 key pair
- signs the supplied pHash with:
  - `embed_phash = 0`
  - `append_pk = 0`
- writes:
  - `payload.bin` = `salt || signature`
  - `pk.bin`
  - `phash.bin`

### Verify

```bash
unified-api/stego_payload_tool verify <payload.bin> <pk.bin> <phash.bin>
```

This verifies the extracted payload using the real unified BLS verification path.

## Running the Demo

Prerequisites:

- `ffmpeg`
- `cmake`
- `build-essential`
- `libopencv-dev`
- `libgmp-dev`
- `libssl-dev`

On Debian/Ubuntu:

```bash
sudo apt install -y ffmpeg cmake build-essential libopencv-dev libgmp-dev libssl-dev
```

Then run:

```bash
./scripts/stardust_image_demo.sh
```

## Example Success Criteria

The demo is considered successful when the summary shows:

```text
Payload round-trip   : OK
BLS verification     : VALID
```

This proves:

- the payload generated by our BLS stego flow survives Stardust embedding
- the payload can be extracted from the watermarked image
- the extracted payload still verifies cryptographically

## Artifacts Produced

The script writes intermediate files to:

```text
tmp_stardust_demo/
```

Typical contents:

- `cover.yuv` - synthetic cover frame
- `embedded.yuv` - watermarked frame
- `embedded.png` - PNG rendered from the watermarked frame
- `reference.y` - original luma plane used by extractor
- `reference444.yuv` - reference frame for alignment
- `aligned/` - Stardust alignment output
- `payload.bin` - original BLS stego payload
- `extracted_payload.bin` - payload recovered by Stardust
- `pk.bin` - public key used for verification
- `phash.bin` - original pHash used for verification
- `generate.txt` / `extract.txt` / `verify.txt` - captured logs

## Notes and Limitations

### 1. Current flow is image-only

This demo uses a single synthetic still frame. It does not yet integrate with the
video Merkle-tree flow.

### 2. Current flow uses a synthetic cover

The script uses FFmpeg `testsrc` to generate a deterministic cover frame. This
keeps the demo self-contained and reproducible.

### 3. Extraction currently depends on alignment

The script uses Stardust `align` before `extract`. This is the realistic path for
watermark recovery from a rendered PNG.

### 4. Only the transport layer is Stardust

Stardust is used as the watermark/stego transport layer. Signature generation and
verification remain our own code.

### 5. Payload size limit

The current implementation uses Stardust's watermark-ID path, which supports up to
256 bits. That is why this demo targets BLS-BN158 without embedded pHash.

## Future Extensions

- use the same transport for video interval checkpoints / Merkle roots
- add support for carrying ledger lookup keys or signature prefixes instead of full payloads
- investigate whether Stardust payload-file mode can support larger arbitrary payloads
- integrate the script into the Docker workflow
