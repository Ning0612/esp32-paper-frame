#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pf_display/packed_framebuffer.hpp"

namespace pf_display {

// Nine simplified weather condition groups, matching OpenWeatherMap's icon
// code groups (see docs/adr/0005-weather-worker-and-status-bar.md). Icons
// are drawn procedurally from circles and lines rather than as bitmap
// assets, so there is no third-party art to license.
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

// Icon shapes are clipped to their declared size x size box so a cloud
// lobe or ray never bleeds into an adjacent status-bar element even
// though the geometry that composes them (circles, base bands) is drawn
// slightly oversized for a rounder look.
struct ClipBox {
    long x0;
    long y0;
    long x1;  // exclusive
    long y1;  // exclusive
};

inline bool clipped_set_pixel(
    PackedFramebufferView& view,
    const long x,
    const long y,
    const ClipBox& clip,
    const Color color)
{
    if (x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1) {
        return false;
    }
    return view.set_pixel(
        static_cast<std::size_t>(x), static_cast<std::size_t>(y), color);
}

inline void fill_circle(
    PackedFramebufferView& view,
    const long center_x,
    const long center_y,
    const long radius,
    const ClipBox& clip,
    const Color color)
{
    for (long dy = -radius; dy <= radius; ++dy) {
        for (long dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            clipped_set_pixel(
                view, center_x + dx, center_y + dy, clip, color);
        }
    }
}

// Standard integer Bresenham line.
inline void draw_line(
    PackedFramebufferView& view,
    long x0,
    long y0,
    const long x1,
    const long y1,
    const ClipBox& clip,
    const Color color)
{
    const long dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const long sx = x0 < x1 ? 1 : -1;
    const long dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    const long sy = y0 < y1 ? 1 : -1;
    long err = dx + dy;
    // Chebyshev distance plus headroom: a correct Bresenham walk never
    // takes more steps than this, so the bound only guards against a
    // latent arithmetic bug turning into a hang instead of a bad pixel.
    long steps_remaining = (dx > -dy ? dx : -dy) + 2;
    while (steps_remaining-- > 0) {
        clipped_set_pixel(view, x0, y0, clip, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const long e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// A rounded cloud silhouette: a flat base band with three overlapping
// circular lobes across the top.
inline void draw_cloud(
    PackedFramebufferView& view,
    const long origin_x,
    const long origin_y,
    const long size,
    const ClipBox& clip,
    const Color color)
{
    const long base_y = origin_y + size - (size / 4);
    for (long y = base_y; y < origin_y + size; ++y) {
        for (long x = origin_x + (size / 6);
             x < origin_x + size - (size / 6);
             ++x) {
            clipped_set_pixel(view, x, y, clip, color);
        }
    }
    fill_circle(
        view, origin_x + size / 3, base_y, size / 3, clip, color);
    fill_circle(
        view, origin_x + size / 2, base_y - size / 6, size / 3, clip,
        color);
    fill_circle(
        view, origin_x + (2 * size) / 3, base_y, size / 3, clip, color);
}

}  // namespace detail

// Draws a size x size icon with its top-left corner at (origin_x,
// origin_y). WeatherIconId::unknown draws nothing, leaving the background
// untouched. Drawing never touches a pixel outside the declared box (see
// detail::ClipBox); size is capped well above the real 32px status-bar
// icon so the long arithmetic below cannot be driven into overflow by a
// pathological caller.
inline void draw_weather_icon(
    PackedFramebufferView& view,
    const std::size_t origin_x,
    const std::size_t origin_y,
    const WeatherIconId icon,
    std::size_t size)
{
    constexpr std::size_t kMaxIconSize = 4096U;
    if (!view.valid() || size == 0U) {
        return;
    }
    if (size > kMaxIconSize) {
        size = kMaxIconSize;
    }
    const long ox = static_cast<long>(origin_x);
    const long oy = static_cast<long>(origin_y);
    const long s = static_cast<long>(size);
    const long cx = ox + s / 2;
    const long cy = oy + s / 2;
    const detail::ClipBox clip{ox, oy, ox + s, oy + s};

    switch (icon) {
        case WeatherIconId::clear: {
            detail::fill_circle(view, cx, cy, s / 3, clip, Color::yellow);
            const long inner = (s / 3) + 2;
            const long outer = s / 2;
            constexpr long kDirs[8][2] = {
                {1, 0},
                {1, 1},
                {0, 1},
                {-1, 1},
                {-1, 0},
                {-1, -1},
                {0, -1},
                {1, -1},
            };
            for (const auto& dir : kDirs) {
                detail::draw_line(
                    view,
                    cx + (dir[0] * inner),
                    cy + (dir[1] * inner),
                    cx + (dir[0] * outer),
                    cy + (dir[1] * outer),
                    clip,
                    Color::yellow);
            }
            break;
        }
        case WeatherIconId::few_clouds:
            detail::fill_circle(
                view, ox + s / 3, oy + s / 3, s / 5, clip, Color::yellow);
            detail::draw_cloud(
                view, ox + s / 6, oy + s / 3, (2 * s) / 3, clip,
                Color::black);
            break;
        case WeatherIconId::clouds:
            detail::draw_cloud(
                view, ox, oy + s / 6, s, clip, Color::black);
            break;
        case WeatherIconId::overcast:
            detail::draw_cloud(
                view, ox, oy + s / 8, (5 * s) / 6, clip, Color::black);
            detail::draw_cloud(
                view, ox + s / 6, oy + s / 3, (5 * s) / 6, clip,
                Color::black);
            break;
        case WeatherIconId::shower_rain:
            detail::draw_cloud(
                view, ox, oy, (5 * s) / 6, clip, Color::black);
            for (int i = 0; i < 3; ++i) {
                const long x = ox + (s / 4) + (i * (s / 4));
                detail::draw_line(
                    view,
                    x,
                    oy + (3 * s) / 4,
                    x,
                    oy + s - 1,
                    clip,
                    Color::blue);
            }
            break;
        case WeatherIconId::rain:
            detail::draw_cloud(
                view, ox, oy, (5 * s) / 6, clip, Color::black);
            for (int i = 0; i < 3; ++i) {
                const long x = ox + (s / 4) + (i * (s / 4));
                detail::draw_line(
                    view,
                    x,
                    oy + (2 * s) / 3,
                    x - 2,
                    oy + s - 1,
                    clip,
                    Color::blue);
            }
            break;
        case WeatherIconId::thunderstorm:
            detail::draw_cloud(
                view, ox, oy, (5 * s) / 6, clip, Color::black);
            detail::draw_line(
                view,
                cx + 2,
                oy + (2 * s) / 3,
                cx - 3,
                oy + (4 * s) / 5,
                clip,
                Color::yellow);
            detail::draw_line(
                view,
                cx - 3,
                oy + (4 * s) / 5,
                cx + 3,
                oy + (4 * s) / 5,
                clip,
                Color::yellow);
            detail::draw_line(
                view,
                cx + 3,
                oy + (4 * s) / 5,
                cx - 2,
                oy + s - 1,
                clip,
                Color::yellow);
            break;
        case WeatherIconId::snow:
            detail::draw_cloud(
                view, ox, oy, (5 * s) / 6, clip, Color::black);
            for (int i = 0; i < 3; ++i) {
                const long x = ox + (s / 4) + (i * (s / 4));
                const long y = oy + (4 * s) / 5;
                detail::fill_circle(view, x, y, 1, clip, Color::blue);
            }
            break;
        case WeatherIconId::mist:
            for (int i = 0; i < 4; ++i) {
                const long y = oy + (s / 5) * (i + 1);
                detail::draw_line(
                    view, ox, y, ox + s - 1, y, clip, Color::black);
            }
            break;
        case WeatherIconId::unknown:
        default:
            break;
    }
}

}  // namespace pf_display
