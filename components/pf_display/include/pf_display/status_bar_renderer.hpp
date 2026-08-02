#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pf_display/bitmap_font.hpp"
#include "pf_display/packed_framebuffer.hpp"
#include "pf_display/weather_icons.hpp"

namespace pf_display {

inline constexpr std::size_t kDeviceIpCapacity = 16U;

inline const char* weekday_abbreviation(const std::uint8_t iso_weekday)
{
    switch (iso_weekday) {
        case 1U:
            return "Mon";
        case 2U:
            return "Tue";
        case 3U:
            return "Wed";
        case 4U:
            return "Thu";
        case 5U:
            return "Fri";
        case 6U:
            return "Sat";
        case 7U:
            return "Sun";
        default:
            return nullptr;
    }
}

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

    bool device_ip_available = false;
    char device_ip[kDeviceIpCapacity]{};

    bool weather_available = false;
    bool weather_stale = false;
    int temperature_rounded = 0;
    char icon_code[8]{};

    bool indoor_available = false;
    int indoor_temperature_rounded = 0;
    int indoor_humidity_rounded = 0;
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

    // Portrait status bars are only 480px wide. The compact scale keeps the
    // date/weather, centered IP, and optional indoor values in three distinct
    // regions without changing the fixed 40px status-bar height.
    const bool compact = view.width() < 640U;
    const std::size_t text_scale = compact ? 2U : 3U;
    const std::size_t margin = compact ? 3U : 4U;
    const std::size_t group_gap = compact ? 3U : 4U;
    const std::size_t icon_size = compact ? 24U : 32U;
    const std::size_t bar_height =
        view.height() < kStatusBarHeight ? view.height() : kStatusBarHeight;
    const std::size_t text_pixel_height = kGlyphHeight * text_scale;
    const std::size_t text_y =
        bar_height > text_pixel_height
            ? (bar_height - text_pixel_height) / 2U
            : 0U;

    char date_text[32]{};
    if (content.time_valid) {
        const char* const weekday =
            weekday_abbreviation(content.iso_weekday);
        if (weekday != nullptr) {
            std::snprintf(
                date_text,
                sizeof(date_text),
                "%04u-%02u-%02u-%s",
                static_cast<unsigned>(content.year),
                static_cast<unsigned>(content.month),
                static_cast<unsigned>(content.day),
                weekday);
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

    const long width = static_cast<long>(view.width());
    const long date_x = static_cast<long>(margin);
    const long date_width = static_cast<long>(text_width(
        std::strlen(date_text), text_scale));
    draw_text(
        view,
        static_cast<std::size_t>(date_x),
        text_y,
        date_text,
        Color::black,
        text_scale);

    long left_end = date_x + date_width;
    if (content.weather_available) {
        const long icon_x = left_end + static_cast<long>(group_gap);
        const long icon_y =
            (static_cast<long>(bar_height) - static_cast<long>(icon_size)) /
            2L;
        if (icon_x >= 0L && icon_x < width && icon_y >= 0L) {
            draw_weather_icon(
                view,
                static_cast<std::size_t>(icon_x),
                static_cast<std::size_t>(icon_y),
                classify_icon_code(content.icon_code),
                icon_size);
        }

        // `^` is the firmware's one-byte degree-mark glyph.
        char temperature_text[16]{};
        std::snprintf(
            temperature_text,
            sizeof(temperature_text),
            "%d^",
            content.temperature_rounded);
        const long temperature_x =
            icon_x + static_cast<long>(icon_size) +
            static_cast<long>(group_gap);
        const long temperature_width = static_cast<long>(text_width(
            std::strlen(temperature_text), text_scale));
        draw_text(
            view,
            temperature_x < 0L ? 0U : static_cast<std::size_t>(temperature_x),
            text_y,
            temperature_text,
            Color::black,
            text_scale);
        left_end = temperature_x + temperature_width;

        if (content.weather_stale) {
            const long stale_x = left_end + static_cast<long>(group_gap);
            draw_text(
                view,
                stale_x < 0L ? 0U : static_cast<std::size_t>(stale_x),
                text_y,
                "*",
                Color::red,
                text_scale);
            left_end = stale_x + static_cast<long>(text_width(1U, text_scale));
        }
    }

    char ip_text[kDeviceIpCapacity]{};
    if (content.device_ip_available && content.device_ip[0] != '\0') {
        std::memcpy(ip_text, content.device_ip, sizeof(ip_text));
        ip_text[sizeof(ip_text) - 1U] = '\0';
    } else {
        std::snprintf(ip_text, sizeof(ip_text), "---.---.---.---");
    }
    const long ip_width = static_cast<long>(text_width(
        std::strlen(ip_text), text_scale));
    const long ip_x = width > ip_width ? (width - ip_width) / 2L : 0L;
    draw_text(
        view,
        ip_x < 0L ? 0U : static_cast<std::size_t>(ip_x),
        text_y,
        ip_text,
        Color::black,
        text_scale);

    if (content.indoor_available) {
        char indoor_text[24]{};
        std::snprintf(
            indoor_text,
            sizeof(indoor_text),
            "%d^ %d%%",
            content.indoor_temperature_rounded,
            content.indoor_humidity_rounded);
        const long indoor_width = static_cast<long>(text_width(
            std::strlen(indoor_text), text_scale));
        const long indoor_x =
            width - static_cast<long>(margin) - indoor_width;
        // At the real 800px/480px widths this region always fits. If a
        // synthetic narrower view is supplied, omit the optional sensor
        // values rather than drawing over the centered IP or wrapping.
        if (indoor_x >= 0L &&
            indoor_x > ip_x + ip_width + static_cast<long>(group_gap) &&
            indoor_x > left_end + static_cast<long>(group_gap)) {
            draw_text(
                view,
                static_cast<std::size_t>(indoor_x),
                text_y,
                indoor_text,
                Color::black,
                text_scale);
        }
    }

    return true;
}

}  // namespace pf_display
