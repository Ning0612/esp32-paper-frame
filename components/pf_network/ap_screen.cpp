#include "pf_network/ap_screen.hpp"

#include <cctype>
#include <cstring>

#include "pf_display/packed_framebuffer.hpp"
#include "qrcode.h"

namespace pf_network {
namespace {

struct Glyph {
    char value;
    std::uint8_t columns[5];
};

constexpr Glyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'a', {0x20, 0x54, 0x54, 0x54, 0x78}},
    {'e', {0x38, 0x54, 0x54, 0x54, 0x18}},
    {'m', {0x7C, 0x04, 0x18, 0x04, 0x78}},
    {'p', {0x7C, 0x14, 0x14, 0x14, 0x08}},
    {'r', {0x7C, 0x08, 0x04, 0x04, 0x08}},
    {'t', {0x08, 0x3E, 0x48, 0x48, 0x20}},
    {'u', {0x3C, 0x40, 0x40, 0x40, 0x3C}},
};

const std::uint8_t* glyph_for(const char value)
{
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.value == value) {
            return glyph.columns;
        }
    }
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.value == '?') {
            return glyph.columns;
        }
    }
    return nullptr;
}

void fill_rect(
    pf_display::PackedFramebufferView& view,
    const std::size_t x,
    const std::size_t y,
    const std::size_t width,
    const std::size_t height,
    const pf_display::Color color)
{
    for (std::size_t row = y; row < y + height; ++row) {
        for (std::size_t column = x;
             column < x + width;
             ++column) {
            view.set_pixel(column, row, color);
        }
    }
}

void draw_text(
    pf_display::PackedFramebufferView& view,
    std::size_t x,
    const std::size_t y,
    const char* const text,
    const std::size_t scale,
    const pf_display::Color color)
{
    if (text == nullptr) {
        return;
    }
    for (std::size_t index = 0U;
         text[index] != '\0';
         ++index) {
        const std::uint8_t* const glyph =
            glyph_for(text[index]);
        if (glyph == nullptr) {
            return;
        }
        for (std::size_t column = 0U; column < 5U; ++column) {
            for (std::size_t row = 0U; row < 7U; ++row) {
                if ((glyph[column] & (1U << row)) != 0U) {
                    fill_rect(
                        view,
                        x + (column * scale),
                        y + (row * scale),
                        scale,
                        scale,
                        color);
                }
            }
        }
        x += 6U * scale;
    }
}

struct QrRenderContext {
    pf_display::PackedFramebufferView* view;
    std::size_t box_x;
    std::size_t box_y;
    std::size_t box_size;
    std::size_t scale;
};

void render_qr(
    const esp_qrcode_handle_t qrcode,
    void* const user_data)
{
    auto& context =
        *static_cast<QrRenderContext*>(user_data);
    const int qr_size = esp_qrcode_get_size(qrcode);
    const std::size_t total =
        (static_cast<std::size_t>(qr_size) + 8U) *
        context.scale;
    const std::size_t origin_x =
        context.box_x +
        ((context.box_size - total) / 2U) +
        (4U * context.scale);
    const std::size_t origin_y =
        context.box_y +
        ((context.box_size - total) / 2U) +
        (4U * context.scale);
    for (int y = 0; y < qr_size; ++y) {
        for (int x = 0; x < qr_size; ++x) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                fill_rect(
                    *context.view,
                    origin_x +
                        (static_cast<std::size_t>(x) *
                         context.scale),
                    origin_y +
                        (static_cast<std::size_t>(y) *
                         context.scale),
                    context.scale,
                    context.scale,
                    pf_display::Color::black);
            }
        }
    }
}

bool generate_qr(
    pf_display::PackedFramebufferView& view,
    const char* const text,
    const std::size_t x,
    const std::size_t y,
    const std::size_t box_size,
    const std::size_t scale)
{
    QrRenderContext context{
        &view,
        x,
        y,
        box_size,
        scale,
    };
    esp_qrcode_config_t configuration =
        ESP_QRCODE_CONFIG_DEFAULT();
    configuration.display_func_with_cb = &render_qr;
    configuration.max_qrcode_version = 10;
    configuration.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    configuration.user_data = &context;
    return esp_qrcode_generate(&configuration, text) == ESP_OK;
}

}  // namespace

bool render_access_point_screen(
    std::uint8_t* const frame,
    const std::size_t length,
    const AccessPointScreenPayload& payload)
{
    if (frame == nullptr ||
        length != pf_display::kFullFramebufferBytes) {
        return false;
    }
    pf_display::PackedFramebufferView view{
        frame,
        length,
        pf_display::kPanelWidth,
        pf_display::kPanelHeight,
    };
    if (!view.fill(pf_display::Color::white)) {
        return false;
    }

    fill_rect(
        view,
        0U,
        0U,
        18U,
        pf_display::kPanelHeight,
        pf_display::Color::blue);
    fill_rect(
        view,
        18U,
        0U,
        500U,
        24U,
        pf_display::Color::yellow);
    fill_rect(
        view,
        518U,
        0U,
        282U,
        24U,
        pf_display::Color::red);
    draw_text(
        view,
        46U,
        46U,
        "PAPERFRAME AP SETUP",
        3U,
        pf_display::Color::black);
    draw_text(
        view,
        46U,
        92U,
        "CONNECT WIFI THEN OPEN WEB UI",
        1U,
        pf_display::Color::blue);

    draw_text(view, 46U, 132U, "SSID", 2U, pf_display::Color::red);
    draw_text(view, 46U, 154U, payload.ssid, 2U, pf_display::Color::black);
    draw_text(view, 46U, 192U, "PASS", 2U, pf_display::Color::red);
    draw_text(view, 46U, 214U, payload.password, 2U, pf_display::Color::black);
    draw_text(view, 46U, 252U, "OPEN", 2U, pf_display::Color::red);
    draw_text(view, 46U, 274U, payload.ip_address, 2U, pf_display::Color::black);

    fill_rect(
        view,
        46U,
        318U,
        430U,
        2U,
        pf_display::Color::black);
    draw_text(
        view,
        46U,
        340U,
        "1 SCAN WIFI QR OR CONNECT MANUALLY",
        1U,
        pf_display::Color::black);
    draw_text(
        view,
        46U,
        360U,
        "2 VISIT 192.168.4.1",
        1U,
        pf_display::Color::black);
    draw_text(
        view,
        46U,
        380U,
        "3 SELECT WIFI AND SAVE",
        1U,
        pf_display::Color::black);
    draw_text(
        view,
        46U,
        428U,
        "DEVICE",
        1U,
        pf_display::Color::green);
    draw_text(
        view,
        98U,
        428U,
        payload.device_suffix,
        1U,
        pf_display::Color::black);

    fill_rect(
        view,
        538U,
        45U,
        220U,
        190U,
        pf_display::Color::white);
    fill_rect(
        view,
        538U,
        255U,
        220U,
        190U,
        pf_display::Color::white);
    if (!generate_qr(
            view,
            payload.wifi_qr,
            538U,
            45U,
            220U,
            4U) ||
        !generate_qr(
            view,
            payload.web_qr,
            538U,
            255U,
            220U,
            5U)) {
        return false;
    }
    draw_text(
        view,
        610U,
        232U,
        "WIFI",
        1U,
        pf_display::Color::blue);
    draw_text(
        view,
        602U,
        442U,
        "WEB UI",
        1U,
        pf_display::Color::green);
    return true;
}

}  // namespace pf_network
