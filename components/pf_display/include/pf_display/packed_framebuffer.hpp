#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace pf_display {

inline constexpr std::size_t kPanelWidth = 800;
inline constexpr std::size_t kPanelHeight = 480;
inline constexpr std::size_t kStatusBarHeight = 40;
inline constexpr std::size_t kLandscapeImageHeight = 440;
inline constexpr std::size_t kPortraitImageWidth = 480;
inline constexpr std::size_t kPortraitImageHeight = 760;
inline constexpr std::size_t kPortraitLogicalHeight =
    kPortraitImageHeight + kStatusBarHeight;
inline constexpr std::uint8_t kPaletteVersion = 1;

enum class Color : std::uint8_t {
    black = 0x0,
    white = 0x1,
    yellow = 0x2,
    red = 0x3,
    blue = 0x5,
    green = 0x6,
};

struct PackedSize {
    bool ok;
    std::size_t bytes;
};

constexpr std::size_t packed_row_bytes(const std::size_t width)
{
    return (width / 2U) + (width % 2U);
}

constexpr PackedSize checked_packed_buffer_bytes(
    const std::size_t width,
    const std::size_t height)
{
    const std::size_t row_bytes = packed_row_bytes(width);
    if (height != 0U &&
        row_bytes > (std::numeric_limits<std::size_t>::max() / height)) {
        return {false, 0U};
    }
    return {true, row_bytes * height};
}

inline constexpr std::size_t kFullFramebufferBytes =
    checked_packed_buffer_bytes(kPanelWidth, kPanelHeight).bytes;
inline constexpr std::size_t kLandscapeImageBytes =
    checked_packed_buffer_bytes(kPanelWidth, kLandscapeImageHeight).bytes;
inline constexpr std::size_t kPortraitImageBytes =
    checked_packed_buffer_bytes(kPortraitImageWidth, kPortraitImageHeight).bytes;
inline constexpr std::size_t kLandscapeStatusBytes =
    checked_packed_buffer_bytes(kPanelWidth, kStatusBarHeight).bytes;
inline constexpr std::size_t kPortraitStatusBytes =
    checked_packed_buffer_bytes(kPortraitImageWidth, kStatusBarHeight).bytes;

constexpr std::uint8_t native_code(const Color color)
{
    return static_cast<std::uint8_t>(color);
}

constexpr bool is_valid_native_code(const std::uint8_t code)
{
    return code == native_code(Color::black) ||
           code == native_code(Color::white) ||
           code == native_code(Color::yellow) ||
           code == native_code(Color::red) ||
           code == native_code(Color::blue) ||
           code == native_code(Color::green);
}

constexpr bool is_valid_color(const Color color)
{
    return is_valid_native_code(native_code(color));
}

inline bool decode_color(const std::uint8_t code, Color& output)
{
    if (!is_valid_native_code(code)) {
        return false;
    }
    output = static_cast<Color>(code);
    return true;
}

inline bool pack_colors(
    const Color even_x,
    const Color odd_x,
    std::uint8_t& output)
{
    if (!is_valid_color(even_x) || !is_valid_color(odd_x)) {
        return false;
    }
    output = static_cast<std::uint8_t>(
        (native_code(even_x) << 4U) | native_code(odd_x));
    return true;
}

class PackedFramebufferView {
public:
    PackedFramebufferView(
        std::uint8_t* const data,
        const std::size_t capacity,
        const std::size_t width,
        const std::size_t height)
        : data_(data),
          capacity_(capacity),
          width_(width),
          height_(height),
          row_bytes_(packed_row_bytes(width))
    {
    }

    bool valid() const
    {
        const PackedSize required =
            checked_packed_buffer_bytes(width_, height_);
        return data_ != nullptr &&
               width_ > 0U &&
               height_ > 0U &&
               required.ok &&
               capacity_ >= required.bytes;
    }

    bool set_pixel(
        const std::size_t x,
        const std::size_t y,
        const Color color)
    {
        if (!contains(x, y) || !is_valid_color(color)) {
            return false;
        }

        std::uint8_t& packed = data_[byte_offset(x, y)];
        const std::uint8_t code = native_code(color);
        if ((x % 2U) == 0U) {
            packed = static_cast<std::uint8_t>(
                (packed & 0x0FU) | (code << 4U));
        } else {
            packed = static_cast<std::uint8_t>((packed & 0xF0U) | code);
        }
        return true;
    }

    bool get_pixel(
        const std::size_t x,
        const std::size_t y,
        Color& output) const
    {
        if (!contains(x, y)) {
            return false;
        }

        const std::uint8_t packed = data_[byte_offset(x, y)];
        const std::uint8_t code = (x % 2U) == 0U
                                      ? static_cast<std::uint8_t>(packed >> 4U)
                                      : static_cast<std::uint8_t>(packed & 0x0FU);
        return decode_color(code, output);
    }

    bool fill(const Color color)
    {
        if (!valid() || !is_valid_color(color)) {
            return false;
        }

        const std::uint8_t code = native_code(color);
        const std::uint8_t packed =
            static_cast<std::uint8_t>((code << 4U) | code);
        const std::size_t length = row_bytes_ * height_;
        for (std::size_t index = 0; index < length; ++index) {
            data_[index] = packed;
        }
        return true;
    }

private:
    bool contains(const std::size_t x, const std::size_t y) const
    {
        return valid() && x < width_ && y < height_;
    }

    std::size_t byte_offset(
        const std::size_t x,
        const std::size_t y) const
    {
        return (y * row_bytes_) + (x / 2U);
    }

    std::uint8_t* data_;
    std::size_t capacity_;
    std::size_t width_;
    std::size_t height_;
    std::size_t row_bytes_;
};

static_assert(kFullFramebufferBytes == 192000U);
static_assert(kLandscapeImageBytes == 176000U);
static_assert(kPortraitImageBytes == 182400U);
static_assert(kLandscapeStatusBytes == 16000U);
static_assert(kPortraitStatusBytes == 9600U);
static_assert(kPortraitLogicalHeight == kPanelWidth);

}  // namespace pf_display
