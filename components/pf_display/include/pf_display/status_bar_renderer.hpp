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
inline constexpr std::size_t kStatusBarWeatherIconSize = 36U;
inline constexpr std::size_t kStatusBarLandscapeTextScale = 4U;
inline constexpr std::size_t kStatusBarPortraitTextScale = 3U;
inline constexpr std::size_t kStatusBarMinimumTextScale = 2U;

static_assert(
    kStatusBarHeight >= kStatusBarWeatherIconSize + 4U,
    "the weather icon must retain a two-pixel vertical inset");

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
    // four information groups readable while the content-width-based layout
    // below gives every gap between adjacent groups the same size.
    const bool compact = view.width() < 640U;
    const std::size_t preferred_text_scale =
        compact ? kStatusBarPortraitTextScale : kStatusBarLandscapeTextScale;
    const std::size_t margin = compact ? 3U : 4U;
    const std::size_t content_gap = compact ? 3U : 4U;
    const std::size_t bar_height =
        view.height() < kStatusBarHeight ? view.height() : kStatusBarHeight;
    const std::size_t icon_size =
        bar_height < kStatusBarWeatherIconSize
            ? bar_height
            : kStatusBarWeatherIconSize;

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

    char temperature_text[16]{};
    if (content.weather_available) {
        std::snprintf(
            temperature_text,
            sizeof(temperature_text),
            "%d^",
            content.temperature_rounded);
    }

    char ip_text[kDeviceIpCapacity]{};
    if (content.device_ip_available && content.device_ip[0] != '\0') {
        std::memcpy(ip_text, content.device_ip, sizeof(ip_text));
        ip_text[sizeof(ip_text) - 1U] = '\0';
    } else {
        std::snprintf(ip_text, sizeof(ip_text), "---.---.---.---");
    }

    char indoor_text[24]{};
    if (content.indoor_available) {
        std::snprintf(
            indoor_text,
            sizeof(indoor_text),
            "%d^ %d%%",
            content.indoor_temperature_rounded,
            content.indoor_humidity_rounded);
    }

    const std::size_t group_count =
        2U + (content.weather_available ? 1U : 0U) +
        (content.indoor_available ? 1U : 0U);
    const long width = static_cast<long>(view.width());
    const long inner_width = width > static_cast<long>(margin * 2U)
        ? width - static_cast<long>(margin * 2U)
        : 0L;

    struct GroupWidths {
        std::size_t date = 0U;
        std::size_t temperature = 0U;
        std::size_t stale = 0U;
        std::size_t weather = 0U;
        std::size_t ip = 0U;
        std::size_t indoor = 0U;
        std::size_t total = 0U;
    };
    const auto widths_for_scale = [&](const std::size_t scale) {
        GroupWidths widths{};
        widths.date = text_width(std::strlen(date_text), scale);
        widths.temperature = content.weather_available
            ? text_width(std::strlen(temperature_text), scale)
            : 0U;
        widths.stale = content.weather_available && content.weather_stale
            ? text_width(1U, scale)
            : 0U;
        widths.weather = content.weather_available
            ? icon_size + content_gap + widths.temperature +
                  (content.weather_stale
                       ? content_gap + widths.stale
                       : 0U)
            : 0U;
        widths.ip = text_width(std::strlen(ip_text), scale);
        widths.indoor = content.indoor_available
            ? text_width(std::strlen(indoor_text), scale)
            : 0U;
        widths.total = widths.date + widths.weather + widths.ip +
            widths.indoor;
        return widths;
    };

    // Prefer the larger text size, but retain the compact size when a
    // portrait frame contains the longest valid combination of all groups.
    std::size_t text_scale = preferred_text_scale;
    const std::size_t minimum_text_scale = compact
        ? kStatusBarMinimumTextScale
        : kStatusBarLandscapeTextScale;
    GroupWidths widths = widths_for_scale(text_scale);
    while (widths.total > static_cast<std::size_t>(inner_width) &&
           text_scale > minimum_text_scale) {
        --text_scale;
        widths = widths_for_scale(text_scale);
    }

    const std::size_t text_pixel_height = kGlyphHeight * text_scale;
    const std::size_t text_y =
        bar_height > text_pixel_height
            ? (bar_height - text_pixel_height) / 2U
            : 0U;
    const std::size_t date_width = widths.date;
    const std::size_t temperature_width = widths.temperature;
    const std::size_t weather_width = widths.weather;
    const std::size_t ip_width = widths.ip;
    const long equal_gap =
        group_count > 1U && inner_width > static_cast<long>(widths.total)
            ? (inner_width - static_cast<long>(widths.total)) /
                  static_cast<long>(group_count - 1U)
            : 0L;
    long cursor_x = static_cast<long>(margin);

    draw_text(
        view,
        cursor_x < 0L ? 0U : static_cast<std::size_t>(cursor_x),
        text_y,
        date_text,
        Color::black,
        text_scale);
    cursor_x += static_cast<long>(date_width);

    if (content.weather_available) {
        cursor_x += equal_gap;
        const long icon_x = cursor_x;
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
        const long temperature_x =
            icon_x + static_cast<long>(icon_size) +
            static_cast<long>(content_gap);
        draw_text(
            view,
            temperature_x < 0L ? 0U : static_cast<std::size_t>(temperature_x),
            text_y,
            temperature_text,
            Color::black,
            text_scale);

        if (content.weather_stale) {
            const long stale_x = temperature_x +
                static_cast<long>(temperature_width) +
                static_cast<long>(content_gap);
            draw_text(
                view,
                stale_x < 0L ? 0U : static_cast<std::size_t>(stale_x),
                text_y,
                "*",
                Color::red,
                text_scale);
        }

        cursor_x += static_cast<long>(weather_width);
    }

    cursor_x += equal_gap;
    draw_text(
        view,
        cursor_x < 0L ? 0U : static_cast<std::size_t>(cursor_x),
        text_y,
        ip_text,
        Color::black,
        text_scale);
    cursor_x += static_cast<long>(ip_width);

    if (content.indoor_available) {
        cursor_x += equal_gap;
        draw_text(
            view,
            cursor_x < 0L ? 0U : static_cast<std::size_t>(cursor_x),
            text_y,
            indoor_text,
            Color::black,
            text_scale);
    }

    return true;
}

}  // namespace pf_display
