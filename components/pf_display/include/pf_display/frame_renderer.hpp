#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "pf_display/packed_framebuffer.hpp"

namespace pf_display {

enum class StatusPlacement : std::uint8_t {
    top,
    bottom,
};

enum class PortraitRotation : std::uint8_t {
    clockwise,
    counter_clockwise,
};

enum class RenderStatus : std::uint8_t {
    ok,
    invalid_argument,
    invalid_geometry,
    invalid_length,
    invalid_palette,
};

struct RenderResult {
    RenderStatus status;

    constexpr bool succeeded() const
    {
        return status == RenderStatus::ok;
    }
};

struct PackedRegion {
    const std::uint8_t* data;
    std::size_t length;
    std::size_t width;
    std::size_t height;
};

namespace detail {

constexpr bool is_valid_placement(const StatusPlacement placement)
{
    return placement == StatusPlacement::top ||
           placement == StatusPlacement::bottom;
}

constexpr bool is_valid_rotation(const PortraitRotation rotation)
{
    return rotation == PortraitRotation::clockwise ||
           rotation == PortraitRotation::counter_clockwise;
}

inline bool has_valid_palette(const PackedRegion& region)
{
    for (std::size_t index = 0; index < region.length; ++index) {
        const std::uint8_t packed = region.data[index];
        if (!is_valid_native_code(static_cast<std::uint8_t>(packed >> 4U)) ||
            !is_valid_native_code(
                static_cast<std::uint8_t>(packed & 0x0FU))) {
            return false;
        }
    }
    return true;
}

inline RenderResult validate_region(
    const PackedRegion& region,
    const std::size_t expected_width,
    const std::size_t expected_height)
{
    if (region.data == nullptr) {
        return {RenderStatus::invalid_argument};
    }
    if (region.width != expected_width ||
        region.height != expected_height) {
        return {RenderStatus::invalid_geometry};
    }

    const PackedSize expected =
        checked_packed_buffer_bytes(expected_width, expected_height);
    if (!expected.ok || region.length != expected.bytes) {
        return {RenderStatus::invalid_length};
    }
    if (!has_valid_palette(region)) {
        return {RenderStatus::invalid_palette};
    }
    return {RenderStatus::ok};
}

inline bool ranges_overlap(
    const std::uint8_t* const first,
    const std::size_t first_length,
    const std::uint8_t* const second,
    const std::size_t second_length)
{
    const std::uintptr_t first_begin =
        reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t second_begin =
        reinterpret_cast<std::uintptr_t>(second);
    if (first_length >
            (std::numeric_limits<std::uintptr_t>::max() - first_begin) ||
        second_length >
            (std::numeric_limits<std::uintptr_t>::max() - second_begin)) {
        return true;
    }

    const std::uintptr_t first_end = first_begin + first_length;
    const std::uintptr_t second_end = second_begin + second_length;
    return first_begin < second_end && second_begin < first_end;
}

inline RenderResult validate_output(
    const PackedRegion& status,
    const PackedRegion& image,
    std::uint8_t* const output,
    const std::size_t output_length)
{
    if (output == nullptr) {
        return {RenderStatus::invalid_argument};
    }
    if (output_length != kFullFramebufferBytes) {
        return {RenderStatus::invalid_length};
    }
    if (ranges_overlap(status.data, status.length, output, output_length) ||
        ranges_overlap(image.data, image.length, output, output_length)) {
        return {RenderStatus::invalid_argument};
    }
    return {RenderStatus::ok};
}

inline std::uint8_t native_code_at(
    const PackedRegion& region,
    const std::size_t x,
    const std::size_t y)
{
    const std::uint8_t packed =
        region.data[(y * packed_row_bytes(region.width)) + (x / 2U)];
    return (x % 2U) == 0U
               ? static_cast<std::uint8_t>(packed >> 4U)
               : static_cast<std::uint8_t>(packed & 0x0FU);
}

inline void set_native_code(
    std::uint8_t* const output,
    const std::size_t x,
    const std::size_t y,
    const std::uint8_t code)
{
    std::uint8_t& packed =
        output[(y * packed_row_bytes(kPanelWidth)) + (x / 2U)];
    if ((x % 2U) == 0U) {
        packed = static_cast<std::uint8_t>(
            (packed & 0x0FU) | (code << 4U));
    } else {
        packed = static_cast<std::uint8_t>((packed & 0xF0U) | code);
    }
}

inline void copy_region_rows(
    const PackedRegion& region,
    const std::size_t output_y,
    std::uint8_t* const output)
{
    const std::size_t row_bytes = packed_row_bytes(region.width);
    for (std::size_t row = 0; row < region.height; ++row) {
        std::copy_n(
            region.data + (row * row_bytes),
            row_bytes,
            output + ((output_y + row) * row_bytes));
    }
}

}  // namespace detail

inline RenderResult compose_landscape(
    const PackedRegion status,
    const PackedRegion image,
    const StatusPlacement placement,
    std::uint8_t* const output,
    const std::size_t output_length)
{
    if (!detail::is_valid_placement(placement)) {
        return {RenderStatus::invalid_argument};
    }

    RenderResult result = detail::validate_region(
        status,
        kPanelWidth,
        kStatusBarHeight);
    if (!result.succeeded()) {
        return result;
    }
    result = detail::validate_region(
        image,
        kPanelWidth,
        kLandscapeImageHeight);
    if (!result.succeeded()) {
        return result;
    }
    result = detail::validate_output(
        status,
        image,
        output,
        output_length);
    if (!result.succeeded()) {
        return result;
    }

    const std::size_t status_y =
        placement == StatusPlacement::top ? 0U : kLandscapeImageHeight;
    const std::size_t image_y =
        placement == StatusPlacement::top ? kStatusBarHeight : 0U;
    detail::copy_region_rows(status, status_y, output);
    detail::copy_region_rows(image, image_y, output);
    return {RenderStatus::ok};
}

inline RenderResult compose_portrait(
    const PackedRegion status,
    const PackedRegion image,
    const StatusPlacement placement,
    const PortraitRotation rotation,
    std::uint8_t* const output,
    const std::size_t output_length)
{
    if (!detail::is_valid_placement(placement) ||
        !detail::is_valid_rotation(rotation)) {
        return {RenderStatus::invalid_argument};
    }

    RenderResult result = detail::validate_region(
        status,
        kPortraitImageWidth,
        kStatusBarHeight);
    if (!result.succeeded()) {
        return result;
    }
    result = detail::validate_region(
        image,
        kPortraitImageWidth,
        kPortraitImageHeight);
    if (!result.succeeded()) {
        return result;
    }
    result = detail::validate_output(
        status,
        image,
        output,
        output_length);
    if (!result.succeeded()) {
        return result;
    }

    for (std::size_t logical_y = 0;
         logical_y < kPortraitLogicalHeight;
         ++logical_y) {
        const bool in_status =
            placement == StatusPlacement::top
                ? logical_y < kStatusBarHeight
                : logical_y >= kPortraitImageHeight;
        const PackedRegion& source = in_status ? status : image;
        const std::size_t source_y =
            placement == StatusPlacement::top
                ? (in_status ? logical_y
                             : logical_y - kStatusBarHeight)
                : (in_status ? logical_y - kPortraitImageHeight
                             : logical_y);

        for (std::size_t logical_x = 0;
             logical_x < kPortraitImageWidth;
             ++logical_x) {
            const std::uint8_t code =
                detail::native_code_at(source, logical_x, source_y);
            const std::size_t native_x =
                rotation == PortraitRotation::clockwise
                    ? (kPortraitLogicalHeight - 1U) - logical_y
                    : logical_y;
            const std::size_t native_y =
                rotation == PortraitRotation::clockwise
                    ? logical_x
                    : (kPortraitImageWidth - 1U) - logical_x;
            detail::set_native_code(
                output,
                native_x,
                native_y,
                code);
        }
    }
    return {RenderStatus::ok};
}

}  // namespace pf_display
