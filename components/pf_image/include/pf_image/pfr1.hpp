#pragma once

#include <cstddef>
#include <cstdint>

#include "pf_display/packed_framebuffer.hpp"

// PFR1's optional compressed-payload flag (Pfr1Flags::kCompressed) carries a
// raw DEFLATE stream (no zlib/gzip wrapper), matching the browser packer's
// `CompressionStream('deflate-raw')` output. On-device this is inflated with
// the ESP32-S3 mask ROM's miniz (`tinfl_decompress_mem_to_mem`, zero extra
// flash/library cost); `pio test -e native` has no ESP-IDF component graph
// to link the ROM symbols against, so host builds use system zlib's raw
// (negative-windowBits) inflate mode instead. Both produce byte-identical
// output for a given raw-DEFLATE stream, so this only changes which backend
// performs the inflate, never PFR1's on-disk semantics.
#if defined(ESP_PLATFORM)
#include "miniz.h"
#else
#include <zlib.h>
#endif

namespace pf_image {

inline constexpr std::size_t kPfr1HeaderSize = 32U;
inline constexpr std::size_t kPfr1MaxFilenameBytes = 96U;
inline constexpr std::size_t kPfr1MaxPayloadBytes =
    pf_display::kPortraitImageBytes;
inline constexpr std::size_t kPfr1MaxFileBytes =
    kPfr1HeaderSize + kPfr1MaxFilenameBytes + kPfr1MaxPayloadBytes;

enum class Orientation : std::uint8_t {
    landscape = 0U,
    portrait = 1U,
};

enum class Dithering : std::uint8_t {
    // Values 0 and 3 remain readable for imagefs/PFR1 backward
    // compatibility; the current WebUI only emits values 1 and 2.
    nearest = 0U,
    floyd_steinberg = 1U,
    atkinson = 2U,
    bayer_4x4 = 3U,
};

enum Pfr1Flags : std::uint16_t {
    kMirrorX = 0x0001U,
    kMirrorY = 0x0002U,
    kRotate90Cw = 0x0004U,
    // Payload is a raw-DEFLATE stream (no zlib/gzip wrapper) that decodes to
    // exactly expected_payload_length(width, height) bytes; payload_length
    // holds the compressed byte count, which must be strictly smaller than
    // the uncompressed size (a browser that can't shrink a payload falls
    // back to storing it uncompressed with this bit clear).
    kCompressed = 0x0008U,
};

// Both buffers must be at least kPfr1MaxPayloadBytes: `compressed` stages
// the raw incoming payload bytes while they stream in via feed(), and
// `output` receives the one-shot-decompressed result at finish(). Required
// only when a file's header sets Pfr1Flags::kCompressed; validators that
// only ever see uncompressed files can omit this (default constructor).
struct Pfr1InflateBuffers {
    std::uint8_t* compressed = nullptr;
    std::size_t compressed_capacity = 0U;
    std::uint8_t* output = nullptr;
    std::size_t output_capacity = 0U;
};

inline constexpr std::size_t kPfr1InflateFailed =
    static_cast<std::size_t>(-1);

// Decompresses a raw DEFLATE stream (no zlib/gzip framing) in one shot.
// Returns the number of bytes written to `dst`, or kPfr1InflateFailed.
#if defined(ESP_PLATFORM)
// NOTE: tinfl_decompress_mem_to_mem() has no "bytes of src actually
// consumed" output, so unlike the host backend below it cannot verify that
// `src_len` was fully consumed by the deflate stream. A payload with extra
// bytes appended after a complete, valid deflate stream (but still within
// the header's declared payload_length, so still CRC-covered) would be
// accepted here. This is a known asymmetry, not a safety issue: decoded
// pixel content and stored file size are unaffected either way — accepted
// trailing bytes are inert, never interpreted as anything.
inline std::size_t inflate_raw_deflate(
    const std::uint8_t* const src,
    const std::size_t src_len,
    std::uint8_t* const dst,
    const std::size_t dst_capacity)
{
    return tinfl_decompress_mem_to_mem(dst, dst_capacity, src, src_len, 0);
}
#else
inline std::size_t inflate_raw_deflate(
    const std::uint8_t* const src,
    const std::size_t src_len,
    std::uint8_t* const dst,
    const std::size_t dst_capacity)
{
    z_stream strm{};
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
        return kPfr1InflateFailed;
    }
    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = static_cast<uInt>(src_len);
    strm.next_out = dst;
    strm.avail_out = static_cast<uInt>(dst_capacity);
    const int result = inflate(&strm, Z_FINISH);
    const std::size_t produced = dst_capacity - strm.avail_out;
    inflateEnd(&strm);
    // Require the whole compressed payload to belong to the deflate stream:
    // Z_STREAM_END alone doesn't guarantee src_len was fully consumed, so a
    // valid stream with trailing garbage bytes would otherwise pass.
    return (result == Z_STREAM_END && strm.avail_in == 0U)
               ? produced
               : kPfr1InflateFailed;
}
#endif

enum class ValidationError : std::uint8_t {
    none = 0U,
    incomplete,
    bad_magic,
    unsupported_version,
    invalid_header_size,
    invalid_flags,
    invalid_dimensions,
    invalid_orientation,
    invalid_palette,
    invalid_dithering,
    invalid_reserved,
    invalid_filename_length,
    invalid_filename,
    invalid_payload_length,
    invalid_payload_palette,
    header_crc_mismatch,
    payload_crc_mismatch,
    trailing_data,
    sink_rejected,
    already_failed,
    unsupported_compression,
    payload_inflate_failed,
};

struct Pfr1Header {
    std::uint16_t flags = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    Orientation orientation = Orientation::landscape;
    std::uint8_t palette = 0U;
    Dithering dithering = Dithering::floyd_steinberg;
    std::uint32_t payload_length = 0U;
    std::uint16_t filename_length = 0U;
    std::uint32_t payload_crc32 = 0U;
    std::uint32_t header_crc32 = 0U;
};

using PayloadSink = bool (*)(
    void* context,
    std::uint32_t offset,
    const std::uint8_t* data,
    std::size_t length);

inline constexpr std::uint32_t crc32_update(
    std::uint32_t crc,
    const std::uint8_t* data,
    const std::size_t length)
{
    for (std::size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

inline constexpr std::uint32_t crc32(
    const std::uint8_t* data,
    const std::size_t length)
{
    return ~crc32_update(0xFFFFFFFFU, data, length);
}

inline constexpr std::uint16_t read_u16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8U));
}

inline constexpr std::uint32_t read_u32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

inline constexpr bool valid_dimensions(
    const std::uint16_t width,
    const std::uint16_t height,
    const std::uint8_t orientation)
{
    return (orientation == static_cast<std::uint8_t>(Orientation::landscape) &&
            width == pf_display::kPanelWidth &&
            height == pf_display::kLandscapeImageHeight) ||
           (orientation == static_cast<std::uint8_t>(Orientation::portrait) &&
            width == pf_display::kPortraitImageWidth &&
            height == pf_display::kPortraitImageHeight);
}

inline constexpr std::uint32_t expected_payload_length(
    const std::uint16_t width,
    const std::uint16_t height)
{
    return static_cast<std::uint32_t>(
        pf_display::checked_packed_buffer_bytes(width, height).bytes);
}

inline bool valid_utf8(const std::uint8_t* data, const std::size_t length)
{
    std::size_t index = 0U;
    while (index < length) {
        const std::uint8_t lead = data[index++];
        if (lead < 0x80U) {
            continue;
        }
        std::size_t continuation_count = 0U;
        std::uint32_t code_point = 0U;
        std::uint32_t minimum = 0U;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            continuation_count = 1U;
            code_point = lead & 0x1FU;
            minimum = 0x80U;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            continuation_count = 2U;
            code_point = lead & 0x0FU;
            minimum = 0x800U;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            continuation_count = 3U;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuation_count > length) {
            return false;
        }
        for (std::size_t count = 0U; count < continuation_count; ++count) {
            const std::uint8_t continuation = data[index++];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return false;
        }
    }
    return true;
}

inline bool valid_filename(const std::uint8_t* data, const std::size_t length)
{
    if (data == nullptr || length == 0U || length > kPfr1MaxFilenameBytes ||
        !valid_utf8(data, length)) {
        return false;
    }
    if ((data[0] == static_cast<std::uint8_t>('.') ) ||
        data[length - 1U] == static_cast<std::uint8_t>('.') ||
        data[length - 1U] == static_cast<std::uint8_t>(' ')) {
        return false;
    }
    if (length == 1U && data[0] == static_cast<std::uint8_t>('.')) {
        return false;
    }
    if (length == 2U && data[0] == static_cast<std::uint8_t>('.') &&
        data[1] == static_cast<std::uint8_t>('.')) {
        return false;
    }
    for (std::size_t index = 0U; index < length; ++index) {
        const std::uint8_t value = data[index];
        if (value < 0x20U || value == 0x7FU || value == 0U ||
            value == static_cast<std::uint8_t>('/') ||
            value == static_cast<std::uint8_t>('\\')) {
            return false;
        }
    }
    return true;
}

class Pfr1Validator {
public:
    explicit Pfr1Validator(
        const PayloadSink sink = nullptr,
        void* const sink_context = nullptr,
        const Pfr1InflateBuffers* const inflate_buffers = nullptr)
        : sink_(sink),
          sink_context_(sink_context),
          inflate_buffers_(inflate_buffers)
    {
    }

    bool feed(const std::uint8_t* data, const std::size_t length)
    {
        if (error_ != ValidationError::none) {
            return false;
        }
        if (data == nullptr && length != 0U) {
            error_ = ValidationError::incomplete;
            return false;
        }
        std::size_t consumed = 0U;
        while (consumed < length) {
            if (header_bytes_ < kPfr1HeaderSize) {
                const std::size_t amount =
                    (kPfr1HeaderSize - header_bytes_) < (length - consumed)
                        ? (kPfr1HeaderSize - header_bytes_)
                        : (length - consumed);
                for (std::size_t index = 0U; index < amount; ++index) {
                    raw_header_[header_bytes_ + index] = data[consumed + index];
                }
                header_bytes_ += amount;
                consumed += amount;
                if (header_bytes_ == kPfr1HeaderSize && !parse_header()) {
                    return false;
                }
                continue;
            }

            if (filename_bytes_ < header_.filename_length) {
                const std::size_t amount =
                    (static_cast<std::size_t>(header_.filename_length) -
                     filename_bytes_) < (length - consumed)
                        ? (static_cast<std::size_t>(header_.filename_length) -
                           filename_bytes_)
                        : (length - consumed);
                for (std::size_t index = 0U; index < amount; ++index) {
                    filename_[filename_bytes_ + index] = data[consumed + index];
                }
                filename_bytes_ += amount;
                consumed += amount;
                if (filename_bytes_ == header_.filename_length &&
                    !valid_filename(filename_, filename_bytes_)) {
                    error_ = ValidationError::invalid_filename;
                    return false;
                }
                continue;
            }

            if (payload_bytes_ < header_.payload_length) {
                const std::size_t amount =
                    (static_cast<std::size_t>(header_.payload_length) -
                     payload_bytes_) < (length - consumed)
                        ? (static_cast<std::size_t>(header_.payload_length) -
                           payload_bytes_)
                        : (length - consumed);
                if (is_compressed()) {
                    // Compressed bytes aren't nibble-coded pixels and can't
                    // be forwarded to the sink until the whole stream is
                    // inflated (finish()); stage them for the one-shot
                    // inflate instead.
                    for (std::size_t index = 0U; index < amount; ++index) {
                        inflate_buffers_->compressed[payload_bytes_ + index] =
                            data[consumed + index];
                    }
                } else {
                    if (!valid_payload(data + consumed, amount)) {
                        error_ = ValidationError::invalid_payload_palette;
                        return false;
                    }
                    if (sink_ != nullptr &&
                        !sink_(
                            sink_context_,
                            payload_bytes_,
                            data + consumed,
                            amount)) {
                        error_ = ValidationError::sink_rejected;
                        return false;
                    }
                }
                payload_crc_ = crc32_update(
                    payload_crc_,
                    data + consumed,
                    amount);
                payload_bytes_ += static_cast<std::uint32_t>(amount);
                consumed += amount;
                continue;
            }

            error_ = ValidationError::trailing_data;
            return false;
        }
        return true;
    }

    bool finish()
    {
        if (error_ != ValidationError::none) {
            return false;
        }
        if (header_bytes_ != kPfr1HeaderSize ||
            filename_bytes_ != header_.filename_length ||
            payload_bytes_ != header_.payload_length) {
            error_ = ValidationError::incomplete;
            return false;
        }
        if ((~payload_crc_) != header_.payload_crc32) {
            error_ = ValidationError::payload_crc_mismatch;
            return false;
        }
        if (is_compressed()) {
            const std::uint32_t expected =
                expected_payload_length(header_.width, header_.height);
            const std::size_t produced = inflate_raw_deflate(
                inflate_buffers_->compressed,
                header_.payload_length,
                inflate_buffers_->output,
                inflate_buffers_->output_capacity);
            if (produced == kPfr1InflateFailed ||
                produced != static_cast<std::size_t>(expected)) {
                error_ = ValidationError::payload_inflate_failed;
                return false;
            }
            if (!valid_payload(inflate_buffers_->output, produced)) {
                error_ = ValidationError::invalid_payload_palette;
                return false;
            }
            if (sink_ != nullptr &&
                !sink_(sink_context_, 0U, inflate_buffers_->output, produced)) {
                error_ = ValidationError::sink_rejected;
                return false;
            }
        }
        finalized_ = true;
        return true;
    }

    ValidationError error() const
    {
        return error_;
    }

    bool finalized() const
    {
        return finalized_;
    }

    const Pfr1Header& header() const
    {
        return header_;
    }

    const std::uint8_t* filename() const
    {
        return filename_;
    }

private:
    bool is_compressed() const
    {
        return (header_.flags & static_cast<std::uint16_t>(kCompressed)) != 0U;
    }

    bool parse_header()
    {
        if (raw_header_[0] != static_cast<std::uint8_t>('P') ||
            raw_header_[1] != static_cast<std::uint8_t>('F') ||
            raw_header_[2] != static_cast<std::uint8_t>('R') ||
            raw_header_[3] != static_cast<std::uint8_t>('1')) {
            error_ = ValidationError::bad_magic;
            return false;
        }
        if (raw_header_[4] != 1U) {
            error_ = ValidationError::unsupported_version;
            return false;
        }
        if (raw_header_[5] != kPfr1HeaderSize) {
            error_ = ValidationError::invalid_header_size;
            return false;
        }
        header_.flags = read_u16(raw_header_ + 6U);
        if ((header_.flags &
             static_cast<std::uint16_t>(
                 ~(kMirrorX | kMirrorY | kRotate90Cw | kCompressed))) != 0U) {
            error_ = ValidationError::invalid_flags;
            return false;
        }
        header_.width = read_u16(raw_header_ + 8U);
        header_.height = read_u16(raw_header_ + 10U);
        const std::uint8_t orientation = raw_header_[12];
        if (orientation > static_cast<std::uint8_t>(Orientation::portrait)) {
            error_ = ValidationError::invalid_orientation;
            return false;
        }
        header_.orientation = static_cast<Orientation>(orientation);
        if (!valid_dimensions(header_.width, header_.height, orientation)) {
            error_ = ValidationError::invalid_dimensions;
            return false;
        }
        header_.palette = raw_header_[13];
        if (header_.palette != pf_display::kPaletteVersion) {
            error_ = ValidationError::invalid_palette;
            return false;
        }
        if (raw_header_[14] > static_cast<std::uint8_t>(Dithering::bayer_4x4)) {
            error_ = ValidationError::invalid_dithering;
            return false;
        }
        header_.dithering = static_cast<Dithering>(raw_header_[14]);
        if (raw_header_[15] != 0U || read_u16(raw_header_ + 22U) != 0U) {
            error_ = ValidationError::invalid_reserved;
            return false;
        }
        header_.payload_length = read_u32(raw_header_ + 16U);
        const std::uint32_t max_payload =
            expected_payload_length(header_.width, header_.height);
        if (is_compressed()) {
            // Defense in depth: max_payload is only ever one of the two
            // fixed profile sizes today (both <= kPfr1MaxPayloadBytes)
            // because valid_dimensions() already rejected anything else
            // above, but the compressed staging buffer's guaranteed
            // capacity is kPfr1MaxPayloadBytes specifically — check that
            // bound directly instead of only relying on the profile
            // invariant holding.
            if (header_.payload_length == 0U ||
                header_.payload_length > max_payload ||
                header_.payload_length > kPfr1MaxPayloadBytes) {
                error_ = ValidationError::invalid_payload_length;
                return false;
            }
            if (inflate_buffers_ == nullptr ||
                inflate_buffers_->compressed == nullptr ||
                inflate_buffers_->compressed_capacity < kPfr1MaxPayloadBytes ||
                inflate_buffers_->output == nullptr ||
                inflate_buffers_->output_capacity < kPfr1MaxPayloadBytes) {
                error_ = ValidationError::unsupported_compression;
                return false;
            }
        } else if (header_.payload_length != max_payload ||
                   header_.payload_length > kPfr1MaxPayloadBytes) {
            error_ = ValidationError::invalid_payload_length;
            return false;
        }
        header_.filename_length = read_u16(raw_header_ + 20U);
        if (header_.filename_length == 0U ||
            header_.filename_length > kPfr1MaxFilenameBytes) {
            error_ = ValidationError::invalid_filename_length;
            return false;
        }
        header_.payload_crc32 = read_u32(raw_header_ + 24U);
        header_.header_crc32 = read_u32(raw_header_ + 28U);
        if (crc32(raw_header_, 24U) != header_.header_crc32) {
            error_ = ValidationError::header_crc_mismatch;
            return false;
        }
        payload_crc_ = 0xFFFFFFFFU;
        return true;
    }

    static bool valid_payload(
        const std::uint8_t* data,
        const std::size_t length)
    {
        if (data == nullptr && length != 0U) {
            return false;
        }
        for (std::size_t index = 0U; index < length; ++index) {
            const std::uint8_t packed = data[index];
            if (!pf_display::is_valid_native_code(
                    static_cast<std::uint8_t>(packed >> 4U)) ||
                !pf_display::is_valid_native_code(
                    static_cast<std::uint8_t>(packed & 0x0FU))) {
                return false;
            }
        }
        return true;
    }

    PayloadSink sink_ = nullptr;
    void* sink_context_ = nullptr;
    const Pfr1InflateBuffers* inflate_buffers_ = nullptr;
    std::uint8_t raw_header_[kPfr1HeaderSize]{};
    std::uint8_t filename_[kPfr1MaxFilenameBytes]{};
    std::size_t header_bytes_ = 0U;
    std::size_t filename_bytes_ = 0U;
    std::uint32_t payload_bytes_ = 0U;
    std::uint32_t payload_crc_ = 0xFFFFFFFFU;
    Pfr1Header header_{};
    ValidationError error_ = ValidationError::none;
    bool finalized_ = false;
};

}  // namespace pf_image
