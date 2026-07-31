#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pf_display/bitmap_font.hpp"
#include "pf_display/packed_framebuffer.hpp"
#include "pf_display/weather_icons.hpp"

namespace pf_display {

// Everything the status bar needs to render one frame; the caller (see
// pf_carousel/image_frame.hpp and welcome_frame.hpp) is responsible for
// filling this from the current RuntimeSnapshot and wall clock. See
// docs/adr/0005-weather-worker-and-status-bar.md for the layout and
// font/icon design decisions.
struct StatusBarContent {
    // When false, render a fixed "unsynced" placeholder instead of a
    // fabricated 1970-01-01 date.
    bool time_valid = false;
    std::uint16_t year = 0;
    std::uint8_t month = 0;        // 1-12
    std::uint8_t day = 0;          // 1-31
    std::uint8_t iso_weekday = 0;  // 1 = Monday .. 7 = Sunday; 0 = omit

    bool weather_available = false;
    bool weather_stale = false;
    int temperature_rounded = 0;
    char icon_code[8]{};
};

// Renders the status bar content into `view` (already sized by the caller
// to either the landscape 800x40 or portrait 480x40 status region) and
// clears it to white first. Returns false only if `view` itself is
// unusable; unknown/unavailable data always renders a safe placeholder
// rather than failing.
inline bool render_status_bar(
    const StatusBarContent& content,
    PackedFramebufferView& view)
{
    if (!view.valid()) {
        return false;
    }
    view.fill(Color::white);

    constexpr std::size_t kTextScale = 3U;
    constexpr std::size_t kMargin = 4U;
    constexpr std::size_t kIconSize = 32U;
    const std::size_t text_y =
        (kStatusBarHeight - (kGlyphHeight * kTextScale)) / 2U;

    char date_text[16]{};
    if (content.time_valid) {
        if (content.iso_weekday >= 1U && content.iso_weekday <= 7U) {
            std::snprintf(
                date_text,
                sizeof(date_text),
                "%04u-%02u-%02u-%u",
                static_cast<unsigned>(content.year),
                static_cast<unsigned>(content.month),
                static_cast<unsigned>(content.day),
                static_cast<unsigned>(content.iso_weekday));
        } else {
            std::snprintf(
                date_text,
                sizeof(date_text),
                "%04u-%02u-%02u",
                static_cast<unsigned>(content.year),
                static_cast<unsigned>(content.month),
                static_cast<unsigned>(content.day));
        }
    } else {
        std::snprintf(date_text, sizeof(date_text), "----");
    }
    draw_text(view, kMargin, text_y, date_text, Color::black, kTextScale);

    if (!content.weather_available) {
        return true;
    }

    // Bail out of the right-hand weather layout on a view too narrow to
    // hold it rather than underflowing the unsigned position math below;
    // the real panel widths (800/480) are always comfortably wide enough.
    const long width = static_cast<long>(view.width());
    const long icon_x =
        width - static_cast<long>(kIconSize) - static_cast<long>(kMargin);
    if (icon_x < 0) {
        return true;
    }
    const long icon_y =
        (static_cast<long>(kStatusBarHeight) -
         static_cast<long>(kIconSize)) /
        2L;
    const WeatherIconId icon = classify_icon_code(content.icon_code);
    draw_weather_icon(
        view,
        static_cast<std::size_t>(icon_x),
        static_cast<std::size_t>(icon_y),
        icon,
        kIconSize);

    // 16 bytes comfortably covers pf_weather::parse_current_weather's
    // validated +-200 degree range ("-200^" is 6 chars); sized generously
    // beyond that so a future range change can't silently truncate.
    char temperature_text[16]{};
    std::snprintf(
        temperature_text,
        sizeof(temperature_text),
        "%d^",
        content.temperature_rounded);
    const std::size_t temperature_length = std::strlen(temperature_text);
    const long temperature_pixel_width = static_cast<long>(
        text_width(temperature_length, kTextScale));
    const long temperature_x =
        icon_x - static_cast<long>(kMargin) - temperature_pixel_width;
    if (temperature_x < 0) {
        return true;
    }
    draw_text(
        view,
        static_cast<std::size_t>(temperature_x),
        text_y,
        temperature_text,
        Color::black,
        kTextScale);

    if (content.weather_stale) {
        const long stale_width =
            static_cast<long>(text_width(1U, kTextScale));
        const long stale_x =
            temperature_x - static_cast<long>(kMargin) - stale_width;
        if (stale_x >= 0) {
            draw_text(
                view,
                static_cast<std::size_t>(stale_x),
                text_y,
                "*",
                Color::red,
                kTextScale);
        }
    }

    return true;
}

}  // namespace pf_display
