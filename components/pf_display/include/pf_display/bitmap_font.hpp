#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_display/packed_framebuffer.hpp"

namespace pf_display {

// Minimal self-authored bitmap font for the status bar (see
// docs/adr/0005-weather-worker-and-status-bar.md): a 3x5 dot-matrix style,
// covering only the digits, punctuation, and weekday letters needed by the
// status bar. It is not a general-purpose font.
inline constexpr std::size_t kGlyphWidth = 3U;
inline constexpr std::size_t kGlyphHeight = 5U;

struct Glyph {
    // Each entry holds one row, top to bottom; bit (kGlyphWidth - 1 - col)
    // is set when that column is foreground. Only the low kGlyphWidth bits
    // are used.
    std::uint8_t rows[kGlyphHeight];
};

inline bool glyph_for(const char character, Glyph& out)
{
    switch (character) {
        case '0':
            out = Glyph{{0b111, 0b101, 0b101, 0b101, 0b111}};
            return true;
        case '1':
            out = Glyph{{0b010, 0b110, 0b010, 0b010, 0b111}};
            return true;
        case '2':
            out = Glyph{{0b111, 0b001, 0b111, 0b100, 0b111}};
            return true;
        case '3':
            out = Glyph{{0b111, 0b001, 0b111, 0b001, 0b111}};
            return true;
        case '4':
            out = Glyph{{0b101, 0b101, 0b111, 0b001, 0b001}};
            return true;
        case '5':
            out = Glyph{{0b111, 0b100, 0b111, 0b001, 0b111}};
            return true;
        case '6':
            out = Glyph{{0b111, 0b100, 0b111, 0b101, 0b111}};
            return true;
        case '7':
            out = Glyph{{0b111, 0b001, 0b001, 0b001, 0b001}};
            return true;
        case '8':
            out = Glyph{{0b111, 0b101, 0b111, 0b101, 0b111}};
            return true;
        case '9':
            out = Glyph{{0b111, 0b101, 0b111, 0b001, 0b111}};
            return true;
        case '-':
            out = Glyph{{0b000, 0b000, 0b111, 0b000, 0b000}};
            return true;
        case '^':  // Stand-in for a degree mark: plain ASCII avoids the
                   // multi-byte UTF-8 encoding of the real "°" character.
            out = Glyph{{0b110, 0b110, 0b000, 0b000, 0b000}};
            return true;
        case '*':
            out = Glyph{{0b101, 0b010, 0b111, 0b010, 0b101}};
            return true;
        case '.':
            out = Glyph{{0b000, 0b000, 0b000, 0b000, 0b010}};
            return true;
        case '%':
            out = Glyph{{0b101, 0b001, 0b010, 0b100, 0b101}};
            return true;
        case 'M':
            out = Glyph{{0b101, 0b111, 0b111, 0b101, 0b101}};
            return true;
        case 'T':
            out = Glyph{{0b111, 0b010, 0b010, 0b010, 0b010}};
            return true;
        case 'W':
            out = Glyph{{0b101, 0b101, 0b111, 0b111, 0b101}};
            return true;
        case 'F':
            out = Glyph{{0b111, 0b100, 0b110, 0b100, 0b100}};
            return true;
        case 'S':
            out = Glyph{{0b111, 0b100, 0b111, 0b001, 0b111}};
            return true;
        case 'a':
            out = Glyph{{0b000, 0b010, 0b101, 0b111, 0b101}};
            return true;
        case 'd':
            out = Glyph{{0b001, 0b011, 0b101, 0b101, 0b011}};
            return true;
        case 'e':
            out = Glyph{{0b000, 0b111, 0b100, 0b110, 0b111}};
            return true;
        case 'h':
            out = Glyph{{0b100, 0b100, 0b111, 0b101, 0b101}};
            return true;
        case 'i':
            out = Glyph{{0b010, 0b000, 0b010, 0b010, 0b010}};
            return true;
        case 'n':
            out = Glyph{{0b000, 0b110, 0b101, 0b101, 0b101}};
            return true;
        case 'o':
            out = Glyph{{0b000, 0b111, 0b101, 0b111, 0b000}};
            return true;
        case 'r':
            out = Glyph{{0b000, 0b110, 0b100, 0b100, 0b100}};
            return true;
        case 't':
            out = Glyph{{0b010, 0b111, 0b010, 0b010, 0b011}};
            return true;
        case 'u':
            out = Glyph{{0b000, 0b101, 0b101, 0b101, 0b111}};
            return true;
        case ' ':
            out = Glyph{{0b000, 0b000, 0b000, 0b000, 0b000}};
            return true;
        default:
            return false;
    }
}

// Draws text left to right starting at (x, y), each glyph scaled by
// `scale` device pixels per glyph pixel with one blank scaled column of
// spacing between characters. Unsupported characters render as a blank
// space rather than aborting the whole string. Returns false only for
// invalid arguments (null text, zero scale, or a view too small to be
// usable at all).
inline bool draw_text(
    PackedFramebufferView& view,
    const std::size_t x,
    const std::size_t y,
    const char* const text,
    const Color foreground,
    const std::size_t scale)
{
    if (text == nullptr || scale == 0U || !view.valid()) {
        return false;
    }

    std::size_t cursor_x = x;
    for (std::size_t index = 0U; text[index] != '\0'; ++index) {
        Glyph glyph{};
        const bool known = glyph_for(text[index], glyph);
        if (known) {
            for (std::size_t row = 0U; row < kGlyphHeight; ++row) {
                for (std::size_t col = 0U; col < kGlyphWidth; ++col) {
                    const std::uint8_t bit = static_cast<std::uint8_t>(
                        1U << (kGlyphWidth - 1U - col));
                    if ((glyph.rows[row] & bit) == 0U) {
                        continue;
                    }
                    for (std::size_t sy = 0U; sy < scale; ++sy) {
                        for (std::size_t sx = 0U; sx < scale; ++sx) {
                            view.set_pixel(
                                cursor_x + (col * scale) + sx,
                                y + (row * scale) + sy,
                                foreground);
                        }
                    }
                }
            }
        }
        cursor_x += (kGlyphWidth + 1U) * scale;
    }
    return true;
}

// Total pixel width drawn by draw_text() for a string of `length`
// characters at the given scale; useful for right-aligning text.
inline constexpr std::size_t text_width(
    const std::size_t length,
    const std::size_t scale)
{
    return length == 0U ? 0U : (length * (kGlyphWidth + 1U) - 1U) * scale;
}

}  // namespace pf_display
