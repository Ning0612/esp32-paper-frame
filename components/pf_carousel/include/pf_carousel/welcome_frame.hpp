#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "pf_display/packed_framebuffer.hpp"

namespace pf_carousel {
namespace detail {

inline void fill_rect(
    std::uint8_t* const frame,
    const std::size_t x,
    const std::size_t y,
    const std::size_t width,
    const std::size_t height,
    const pf_display::Color color)
{
    const std::uint8_t code = pf_display::native_code(color);
    const std::uint8_t packed =
        static_cast<std::uint8_t>((code << 4U) | code);
    const std::size_t row_bytes =
        pf_display::packed_row_bytes(pf_display::kPanelWidth);
    const std::size_t rectangle_bytes = width / 2U;
    for (std::size_t row = 0; row < height; ++row) {
        std::fill_n(
            frame + ((y + row) * row_bytes) + (x / 2U),
            rectangle_bytes,
            packed);
    }
}

}  // namespace detail

inline bool render_welcome_frame(
    std::uint8_t* const frame,
    const std::size_t length)
{
    if (frame == nullptr ||
        length != pf_display::kFullFramebufferBytes) {
        return false;
    }

    const std::uint8_t white =
        pf_display::native_code(pf_display::Color::white);
    std::fill_n(
        frame,
        length,
        static_cast<std::uint8_t>((white << 4U) | white));

    constexpr std::size_t border = 12U;
    detail::fill_rect(
        frame,
        0U,
        0U,
        pf_display::kPanelWidth,
        border,
        pf_display::Color::blue);
    detail::fill_rect(
        frame,
        0U,
        pf_display::kPanelHeight - border,
        pf_display::kPanelWidth,
        border,
        pf_display::Color::blue);
    detail::fill_rect(
        frame,
        0U,
        border,
        border,
        pf_display::kPanelHeight - (2U * border),
        pf_display::Color::blue);
    detail::fill_rect(
        frame,
        pf_display::kPanelWidth - border,
        border,
        border,
        pf_display::kPanelHeight - (2U * border),
        pf_display::Color::blue);

    constexpr pf_display::Color status_colors[] = {
        pf_display::Color::black,
        pf_display::Color::yellow,
        pf_display::Color::red,
        pf_display::Color::blue,
        pf_display::Color::green,
        pf_display::Color::white,
    };
    for (std::size_t index = 0; index < 6U; ++index) {
        detail::fill_rect(
            frame,
            40U + (index * 120U),
            32U,
            120U,
            24U,
            status_colors[index]);
    }

    // A dependency-free PaperFrame "PF" mark keeps the bootstrap frame
    // available before weather data or an image catalog exist.
    detail::fill_rect(
        frame, 240U, 140U, 24U, 180U, pf_display::Color::black);
    detail::fill_rect(
        frame, 240U, 140U, 120U, 24U, pf_display::Color::black);
    detail::fill_rect(
        frame, 240U, 220U, 120U, 24U, pf_display::Color::black);
    detail::fill_rect(
        frame, 336U, 140U, 24U, 104U, pf_display::Color::black);

    detail::fill_rect(
        frame, 440U, 140U, 24U, 180U, pf_display::Color::black);
    detail::fill_rect(
        frame, 440U, 140U, 120U, 24U, pf_display::Color::black);
    detail::fill_rect(
        frame, 440U, 220U, 96U, 24U, pf_display::Color::black);

    constexpr pf_display::Color accent_colors[] = {
        pf_display::Color::black,
        pf_display::Color::red,
        pf_display::Color::yellow,
        pf_display::Color::green,
        pf_display::Color::blue,
        pf_display::Color::white,
    };
    for (std::size_t index = 0; index < 6U; ++index) {
        detail::fill_rect(
            frame,
            220U + (index * 60U),
            360U,
            60U,
            32U,
            accent_colors[index]);
    }
    return true;
}

// A single all-white frame with no status content, used for the one-time
// AWAY refresh (Guild.md 4.9: "只執行一次全白刷新"). Submitting it through
// the normal refresh path already puts the panel to sleep afterward, so
// no separate blank/sleep-only API is needed.
inline bool render_blank_frame(
    std::uint8_t* const frame,
    const std::size_t length)
{
    if (frame == nullptr || length != pf_display::kFullFramebufferBytes) {
        return false;
    }
    const std::uint8_t white = pf_display::native_code(pf_display::Color::white);
    std::fill_n(
        frame, length, static_cast<std::uint8_t>((white << 4U) | white));
    return true;
}

}  // namespace pf_carousel
