#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
STARDUST = ROOT / "stardust"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise RuntimeError(f"Expected text not found in {path}")
    path.write_text(text.replace(old, new, 1))


def main() -> None:
    # Use HTTPS for OpenCV submodules instead of SSH.
    replace_once(
        STARDUST / ".gitmodules",
        'url = git@github.com:opencv/opencv_contrib.git',
        'url = https://github.com/opencv/opencv_contrib.git',
    )
    replace_once(
        STARDUST / ".gitmodules",
        'url = git@github.com:opencv/opencv.git',
        'url = https://github.com/opencv/opencv.git',
    )

    # Fix system OpenCV workflow in align.
    replace_once(
        STARDUST / "align" / "CMakeLists.txt",
        '# Find OpenCV package\nif (SD_BUILD_OPENCV)\n',
        '# Find OpenCV package whenever OpenCV support is enabled.\n'
        '# Upstream only does this when SD_BUILD_OPENCV is ON, which breaks using a\n'
        '# system-installed OpenCV with SD_BUILD_OPENCV=OFF.\n'
        'if (SD_WITH_OPENCV)\n',
    )

    # Fix system OpenCV workflow in extract.
    replace_once(
        STARDUST / "extract" / "CMakeLists.txt",
        '# Find OpenCV package\nif (SD_BUILD_OPENCV)\n',
        '# Find OpenCV package whenever OpenCV support is enabled.\n'
        '# Upstream only does this when SD_BUILD_OPENCV is ON, which breaks using a\n'
        '# system-installed OpenCV with SD_BUILD_OPENCV=OFF.\n'
        'if (SD_WITH_OPENCV)\n',
    )

    # Avoid extractor crash for payloads > 64 bits when printing decimal WM ID.
    replace_once(
        STARDUST / "extract" / "extract.cpp",
        'std::string convert_bin_to_dec(const int8_t *bin) {\n'
        '    return std::to_string(std::stoull(std::string((const char *)bin), nullptr, 2));\n'
        '}\n',
        'std::string convert_bin_to_dec(const int8_t *bin) {\n'
        '    const std::string s((const char *)bin);\n'
        '    try {\n'
        '        return std::to_string(std::stoull(s, nullptr, 2));\n'
        '    } catch (const std::out_of_range &) {\n'
        '        // Large payloads (for example our 184-bit BLS stego payload) do not fit\n'
        '        // into uint64_t. Keep decimal output best-effort and avoid crashing.\n'
        '        return "<overflow>";\n'
        '    }\n'
        '}\n',
    )

    print("Patched stardust for local demo use")


if __name__ == "__main__":
    main()
