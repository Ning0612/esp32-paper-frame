# Asset Credits

The MIT `LICENSE` at the root of this repository applies to original
source code only.

## Weather status-bar icons

The 9 weather condition icons baked into
`components/pf_display/include/pf_display/weather_icon_bitmaps.hpp` are
converted (rasterized, downsampled to 32x32, thresholded to 1-bit
monochrome, and packed) from icon glyphs in the
[erikflowers/weather-icons](https://github.com/erikflowers/weather-icons)
project, pinned to commit `bb80982bf1f43f2d57f9dd753e7413bf88beb9ed`.

| `WeatherIconId` | Source glyph |
| --- | --- |
| `clear` | `svg/wi-day-sunny.svg` |
| `few_clouds` | `svg/wi-day-cloudy.svg` |
| `clouds` | `svg/wi-cloud.svg` |
| `overcast` | `svg/wi-cloudy.svg` |
| `shower_rain` | `svg/wi-showers.svg` |
| `rain` | `svg/wi-rain.svg` |
| `thunderstorm` | `svg/wi-thunderstorm.svg` |
| `snow` | `svg/wi-snow.svg` |
| `mist` | `svg/wi-fog.svg` |

Copyright notice (as required by OFL-1.1 section 2, reproduced from
upstream's own credit): icon designs originally by Lukas Bischoff
(http://www.twitter.com/artill); icon art and web font by Erik Flowers
(http://www.helloerik.com), with Reserved Font Name "Weather Icons".

The icon glyphs themselves are licensed under the **SIL Open Font License
1.1 (OFL-1.1)** (erikflowers/weather-icons' own `package.json` "MIT" field
covers only its CSS/JS tooling, not the glyph artwork; see that project's
README). The full license text is included verbatim at
[LICENSES/OFL-1.1.txt](LICENSES/OFL-1.1.txt). The converted bitmaps in
this repository are derived from OFL-1.1-licensed artwork and are **not
relicensed under this repository's MIT `LICENSE`**. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md](docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md).

Per OFL-1.1 section 2, this copyright notice and license text must
accompany every copy of the converted bitmaps -- including compiled
firmware/release artifacts -- not just this source repository.

The original SVG source files and any intermediate rasterized images are
**not committed** to this repository; only the final packed C++ header is.
See `scripts/generate_weather_icons.py` for how to reproduce or update the
conversion.
