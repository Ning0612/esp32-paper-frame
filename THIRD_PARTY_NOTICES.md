# Third-Party Notices

This repository uses the MIT License (see `LICENSE`) for original project
source code only. Third-party assets keep their own terms.

## Weather Icons

- Source: https://github.com/erikflowers/weather-icons
- Pinned commit: `bb80982bf1f43f2d57f9dd753e7413bf88beb9ed`
- Use: 9 source SVG glyphs converted (rasterized, downsampled to 32x32,
  thresholded to 1-bit monochrome, packed into a C++ constant table) into
  `components/pf_display/include/pf_display/weather_icon_bitmaps.hpp` for
  the status-bar weather icon. See `ASSET_CREDITS.md` for the full
  glyph-to-icon mapping and `scripts/generate_weather_icons.py` for the
  conversion process.
- Copyright: icon designs originally by Lukas Bischoff
  (http://www.twitter.com/artill); icon art and web font by Erik Flowers
  (http://www.helloerik.com), with Reserved Font Name "Weather Icons".
- License: SIL Open Font License 1.1 (OFL-1.1), full text at
  [LICENSES/OFL-1.1.txt](LICENSES/OFL-1.1.txt). The converted bitmaps are
  not relicensed under this repository's MIT License; per OFL-1.1 section
  2, this copyright notice and license text must accompany every
  redistributed copy, including compiled firmware/release artifacts.
- The original SVG files (and any intermediate rasterized PNGs) are not
  committed to this repository.
