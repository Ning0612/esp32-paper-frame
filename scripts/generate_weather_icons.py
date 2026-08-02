"""Regenerate components/pf_display/include/pf_display/weather_icon_bitmaps.hpp
from erikflowers/weather-icons source glyphs.

This is a one-time / occasionally-rerun dev tool, not part of the firmware
build. It is NOT invoked by ``pio run`` and has no runtime dependency on the
generated header's content matching any particular source -- it only exists
so the conversion is auditable and reproducible without committing the
original third-party artwork to this repository (see ASSET_CREDITS.md and
docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md).

Provenance (pin exactly this when regenerating):
    Upstream repo:  https://github.com/erikflowers/weather-icons
    Pinned commit:  bb80982bf1f43f2d57f9dd753e7413bf88beb9ed
    License:        SIL Open Font License 1.1 (icons themselves; the repo's
                     own package.json "MIT" field covers only its CSS/JS
                     tooling, not the glyph artwork -- see upstream README).

Regeneration steps:
    1. Fetch the 9 source SVGs listed in ICON_SOURCES below from the pinned
       commit, e.g.:
           gh api -H "Accept: application/vnd.github.raw" \\
             "repos/erikflowers/weather-icons/contents/svg/<name>.svg?ref=bb80982bf1f43f2d57f9dd753e7413bf88beb9ed" \\
             > <name>.svg
       Do not commit these SVGs anywhere in this repository.
    2. Rasterize each SVG to a 256px-wide transparent-background PNG with
       the "resvg-cli" npm package (unscoped -- published by Zhengqbbb, a
       thin CLI wrapper around @resvg/resvg-js; this is NOT the same
       package as the official "@resvg/resvg-js-cli", which does not
       expose a plain --fit-width flag the same way). Pin the version used
       so a later regeneration rasterizes identically:
           npx --yes resvg-cli@2.6.2 --fit-width 256 <name>.svg <name>.png
       No native/system dependency beyond Node.js, avoiding cairosvg's
       cairo/GTK install pain on Windows.
    3. Run this script against the directory of PNGs:
           python scripts/generate_weather_icons.py <png_dir> \\
             components/pf_display/include/pf_display/weather_icon_bitmaps.hpp
       Requires Pillow==12.3.0 (pip install pillow==12.3.0; the exact
       version used originally -- LANCZOS resampling output can shift
       slightly across Pillow versions). Not part of requirements-dev.txt
       since it is only needed when regenerating icons, not for routine
       firmware builds.
    4. Discard the SVGs and PNGs -- only the emitted header is committed.
       The script raises ValueError instead of writing a header if any
       icon rasterizes to an all-foreground or all-background 32x32 block,
       since that shape is never a real glyph and is the signature of an
       opaque (non-transparent-background) input PNG slipping through
       step 2 -- see rasterize_to_rows.

Output format: each icon is a fixed 32x32, 1-bit-per-pixel bitmap (4 bytes
per row, MSB-first -- bit (7 - col%8) of rows[row][col/8] set means "draw
foreground pixel"), matching the row-array convention already used by
pf_display::Glyph in bitmap_font.hpp. Pixels are foreground where the source
PNG's alpha channel is >= 50% coverage, background otherwise -- the source
glyphs are solid black fills on a transparent canvas, so alpha is a direct,
anti-aliasing-robust coverage signal.

The generated header is pure data (no WeatherIconId dependency, so it has no
include-order coupling to weather_icons.hpp); the WeatherIconId -> bitmap
lookup lives in weather_icons.hpp itself, next to draw_weather_icon.
"""

import os
import sys
from pathlib import Path

from PIL import Image

ICON_SIZE = 32

# WeatherIconId (see weather_icons.hpp) -> source glyph file stem, matching
# docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md's table.
ICON_SOURCES = [
    ("clear", "wi-day-sunny"),
    ("few_clouds", "wi-day-cloudy"),
    ("clouds", "wi-cloud"),
    ("overcast", "wi-cloudy"),
    ("shower_rain", "wi-showers"),
    ("rain", "wi-rain"),
    ("thunderstorm", "wi-thunderstorm"),
    ("snow", "wi-snow"),
    ("mist", "wi-fog"),
]


def rasterize_to_rows(png_path: Path) -> list[list[int]]:
    image = Image.open(png_path).convert("RGBA")
    image = image.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
    alpha = image.getchannel("A")

    rows: list[list[int]] = []
    foreground_count = 0
    for y in range(ICON_SIZE):
        row_bytes = [0, 0, 0, 0]
        for x in range(ICON_SIZE):
            if alpha.getpixel((x, y)) >= 128:
                foreground_count += 1
                byte_index = x // 8
                bit_index = 7 - (x % 8)
                row_bytes[byte_index] |= 1 << bit_index
        rows.append(row_bytes)

    total = ICON_SIZE * ICON_SIZE
    if foreground_count == 0 or foreground_count == total:
        raise ValueError(
            f"{png_path}: rasterized to a solid "
            f"{'foreground' if foreground_count else 'background'} block "
            "(no glyph shape) -- input PNG is likely missing a "
            "transparent background"
        )
    return rows


def format_rows(rows: list[list[int]]) -> str:
    lines = []
    for row in rows:
        lines.append(
            "        {"
            + ", ".join(f"0x{b:02x}U" for b in row)
            + "},"
        )
    return "\n".join(lines)


def generate_header(png_dir: Path) -> str:
    parts = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// Generated by scripts/generate_weather_icons.py -- do not hand-edit.",
        "// Source: https://github.com/erikflowers/weather-icons"
        " @ bb80982bf1f43f2d57f9dd753e7413bf88beb9ed (SIL OFL-1.1).",
        "// See ASSET_CREDITS.md, THIRD_PARTY_NOTICES.md and"
        " docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md.",
        "",
        "namespace pf_display {",
        "",
        "struct IconBitmap32 {",
        "    // rows[y][byte]; bit (7 - x%8) of rows[y][x/8] set means"
        " foreground pixel.",
        "    std::uint8_t rows[32][4];",
        "};",
        "",
    ]
    for icon_id, stem in ICON_SOURCES:
        png_path = png_dir / f"{stem}.png"
        rows = rasterize_to_rows(png_path)
        const_name = f"kIconBitmap_{icon_id}"
        parts.append(
            f"inline constexpr IconBitmap32 {const_name} = {{{{"
        )
        parts.append(format_rows(rows))
        parts.append("}};")
        parts.append("")

    parts.append("}  // namespace pf_display")
    parts.append("")
    return "\n".join(parts)


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: generate_weather_icons.py <png_dir> <output_header>",
            file=sys.stderr,
        )
        return 2
    png_dir = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    header_text = generate_header(png_dir)

    # Write to a temp file in the same directory and atomically replace the
    # target so an interrupted write (disk full, process killed) can never
    # leave a truncated header in place -- os.replace is atomic on both
    # POSIX and Windows.
    tmp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    tmp_path.write_text(header_text, encoding="utf-8", newline="\n")
    os.replace(tmp_path, output_path)
    print(f"wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
