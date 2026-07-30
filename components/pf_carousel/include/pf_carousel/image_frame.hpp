#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "pf_display/frame_renderer.hpp"
#include "pf_image/pfr1.hpp"

namespace pf_carousel {

// Decodes one streamed PFR1 file into a caller-owned payload buffer and then
// composes it with a white status strip into the panel framebuffer. The
// decoder is deliberately independent from StorageWorker so it remains
// host-testable; callers only need to forward stream chunks to feed().
class Pfr1FrameDecoder final {
public:
    Pfr1FrameDecoder(
        std::uint8_t* const payload,
        const std::size_t payload_capacity)
        : payload_(payload),
          payload_capacity_(payload_capacity),
          validator_(copy_payload, this)
    {
    }

    bool feed(const std::uint8_t* const data, const std::size_t length)
    {
        return !failed_ && validator_.feed(data, length);
    }

    bool finish_and_compose(
        std::uint8_t* const status,
        const std::size_t status_capacity,
        std::uint8_t* const output,
        const std::size_t output_length,
        const pf_display::StatusPlacement placement =
            pf_display::StatusPlacement::top,
        const pf_display::PortraitRotation portrait_rotation =
            pf_display::PortraitRotation::clockwise)
    {
        if (failed_ || status == nullptr ||
            status_capacity < pf_display::kLandscapeStatusBytes ||
            output == nullptr ||
            !validator_.finish()) {
            return false;
        }

        const std::uint8_t white = pf_display::native_code(
            pf_display::Color::white);
        std::fill_n(
            status,
            pf_display::kLandscapeStatusBytes,
            static_cast<std::uint8_t>((white << 4U) | white));

        const pf_image::Pfr1Header& header = validator_.header();
        if (header.orientation == pf_image::Orientation::landscape) {
            const pf_display::RenderResult result =
                pf_display::compose_landscape(
                    {
                        status,
                        pf_display::kLandscapeStatusBytes,
                        pf_display::kPanelWidth,
                        pf_display::kStatusBarHeight,
                    },
                    {
                        payload_,
                        header.payload_length,
                        pf_display::kPanelWidth,
                        pf_display::kLandscapeImageHeight,
                    },
                    placement,
                    output,
                    output_length);
            return result.succeeded();
        }

        const pf_display::RenderResult result = pf_display::compose_portrait(
            {
                status,
                pf_display::kPortraitStatusBytes,
                pf_display::kPortraitImageWidth,
                pf_display::kStatusBarHeight,
            },
            {
                payload_,
                header.payload_length,
                pf_display::kPortraitImageWidth,
                pf_display::kPortraitImageHeight,
            },
            placement,
            portrait_rotation,
            output,
            output_length);
        return result.succeeded();
    }

    const pf_image::Pfr1Header& header() const
    {
        return validator_.header();
    }

    pf_image::ValidationError error() const
    {
        return validator_.error();
    }

private:
    static bool copy_payload(
        void* const context,
        const std::uint32_t offset,
        const std::uint8_t* const data,
        const std::size_t length)
    {
        auto* const decoder = static_cast<Pfr1FrameDecoder*>(context);
        if (decoder == nullptr || data == nullptr ||
            static_cast<std::size_t>(offset) > decoder->payload_capacity_ ||
            length > decoder->payload_capacity_ - offset) {
            if (decoder != nullptr) {
                decoder->failed_ = true;
            }
            return false;
        }
        std::copy_n(data, length, decoder->payload_ + offset);
        return true;
    }

    std::uint8_t* payload_ = nullptr;
    std::size_t payload_capacity_ = 0U;
    pf_image::Pfr1Validator validator_;
    bool failed_ = false;
};

}  // namespace pf_carousel
