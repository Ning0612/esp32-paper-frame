#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "pf_display/packed_framebuffer.hpp"
#include "pf_display/weather_icon_bitmaps.hpp"

namespace pf_display {

// Nine simplified weather condition groups, matching OpenWeatherMap's icon
// code groups (see docs/adr/0005-weather-worker-and-status-bar.md and
// docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md). Icon
// artwork is converted from erikflowers/weather-icons (SIL OFL-1.1); see
// ASSET_CREDITS.md and THIRD_PARTY_NOTICES.md.
enum class WeatherIconId : std::uint8_t {
    clear,
    few_clouds,
    clouds,
    overcast,
    shower_rain,
    rain,
    thunderstorm,
    snow,
    mist,
    unknown,
};

// Maps an OpenWeatherMap icon code ("01d", "10n", ...) to one of the nine
// categories above. Only the two-digit condition group matters; the
// day/night suffix does not change the drawn shape. Pure and
// host-testable.
inline WeatherIconId classify_icon_code(const char* const icon_code)
{
    if (icon_code == nullptr || icon_code[0] == '\0' ||
        icon_code[1] == '\0') {
        return WeatherIconId::unknown;
    }
    const char group[3] = {icon_code[0], icon_code[1], '\0'};
    if (std::strcmp(group, "01") == 0) {
        return WeatherIconId::clear;
    }
    if (std::strcmp(group, "02") == 0) {
        return WeatherIconId::few_clouds;
    }
    if (std::strcmp(group, "03") == 0) {
        return WeatherIconId::clouds;
    }
    if (std::strcmp(group, "04") == 0) {
        return WeatherIconId::overcast;
    }
    if (std::strcmp(group, "09") == 0) {
        return WeatherIconId::shower_rain;
    }
    if (std::strcmp(group, "10") == 0) {
        return WeatherIconId::rain;
    }
    if (std::strcmp(group, "11") == 0) {
        return WeatherIconId::thunderstorm;
    }
    if (std::strcmp(group, "13") == 0) {
        return WeatherIconId::snow;
    }
    if (std::strcmp(group, "50") == 0) {
        return WeatherIconId::mist;
    }
    return WeatherIconId::unknown;
}

namespace detail {

// WeatherIconId -> converted source bitmap. unknown (and anything outside
// the known set) has no bitmap, matching draw_weather_icon's no-draw
// contract for that case.
inline const IconBitmap32* icon_bitmap_for(const WeatherIconId icon)
{
    switch (icon) {
        case WeatherIconId::clear:
            return &kIconBitmap_clear;
        case WeatherIconId::few_clouds:
            return &kIconBitmap_few_clouds;
        case WeatherIconId::clouds:
            return &kIconBitmap_clouds;
        case WeatherIconId::overcast:
            return &kIconBitmap_overcast;
        case WeatherIconId::shower_rain:
            return &kIconBitmap_shower_rain;
        case WeatherIconId::rain:
            return &kIconBitmap_rain;
        case WeatherIconId::thunderstorm:
            return &kIconBitmap_thunderstorm;
        case WeatherIconId::snow:
            return &kIconBitmap_snow;
        case WeatherIconId::mist:
            return &kIconBitmap_mist;
        case WeatherIconId::unknown:
        default:
            return nullptr;
    }
}

inline bool bitmap_pixel_set(
    const IconBitmap32& bitmap,
    const std::size_t x,
    const std::size_t y)
{
    const std::uint8_t byte = bitmap.rows[y][x / 8U];
    const std::uint8_t bit = static_cast<std::uint8_t>(7U - (x % 8U));
    return ((byte >> bit) & 0x01U) != 0U;
}

}  // namespace detail

// Draws a size x size icon with its top-left corner at (origin_x,
// origin_y) by nearest-neighbor sampling the converted 32x32 source
// bitmap. WeatherIconId::unknown draws nothing, leaving the background
// untouched; likewise size == 0 is a no-op. Only foreground pixels are
// painted (as Color::black) -- background pixels are left as-is, so the
// caller is expected to have already cleared the destination area (see
// render_status_bar's leading view.fill(Color::white)). size is capped
// well above the real 32px status-bar icon so the sampling arithmetic
// below cannot be driven into overflow by a pathological caller.
inline void draw_weather_icon(
    PackedFramebufferView& view,
    const std::size_t origin_x,
    const std::size_t origin_y,
    const WeatherIconId icon,
    std::size_t size)
{
    constexpr std::size_t kMaxIconSize = 4096U;
    constexpr std::size_t kSourceSize = 32U;
    if (!view.valid() || size == 0U) {
        return;
    }
    if (size > kMaxIconSize) {
        size = kMaxIconSize;
    }
    // origin already outside the view can never contribute a valid pixel.
    if (origin_x >= view.width() || origin_y >= view.height()) {
        return;
    }
    // Belt-and-braces against origin_x + dx / origin_y + dy wrapping a
    // size_t back into range: even though real views are bounded by an
    // actually allocated (and therefore small) buffer, PackedFramebufferView
    // itself does not cap width()/height(), so this is checked explicitly
    // rather than relied upon.
    constexpr std::size_t kSizeMax = std::numeric_limits<std::size_t>::max();
    if (origin_x > kSizeMax - (size - 1U) ||
        origin_y > kSizeMax - (size - 1U)) {
        return;
    }
    const IconBitmap32* const bitmap = detail::icon_bitmap_for(icon);
    if (bitmap == nullptr) {
        return;
    }

    for (std::size_t dy = 0U; dy < size; ++dy) {
        const std::size_t sy = (dy * kSourceSize) / size;
        for (std::size_t dx = 0U; dx < size; ++dx) {
            const std::size_t sx = (dx * kSourceSize) / size;
            if (detail::bitmap_pixel_set(*bitmap, sx, sy)) {
                view.set_pixel(origin_x + dx, origin_y + dy, Color::black);
            }
        }
    }
}

}  // namespace pf_display
