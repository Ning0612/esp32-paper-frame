#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "pf_display/epd7in3e.hpp"

namespace pf_display {

class EspIdfPanelTransport final : public PanelTransport {
public:
    EspIdfPanelTransport() = default;
    ~EspIdfPanelTransport() override;

    EspIdfPanelTransport(const EspIdfPanelTransport&) = delete;
    EspIdfPanelTransport& operator=(const EspIdfPanelTransport&) = delete;
    EspIdfPanelTransport(EspIdfPanelTransport&&) = delete;
    EspIdfPanelTransport& operator=(EspIdfPanelTransport&&) = delete;

    esp_err_t initialize();
    esp_err_t shutdown();
    bool initialized() const;

    bool set_reset_level(bool high) override;
    bool write_command_transaction(std::uint8_t command) override;
    bool write_data_transaction(
        const std::uint8_t* data,
        std::size_t length) override;
    bool busy_is_idle(bool& idle) override;
    std::uint64_t monotonic_ms() const override;
    void delay_ms(std::uint32_t duration_ms) override;

private:
    bool transmit_command(std::uint8_t command);
    bool transmit_data(std::size_t length);
    void reset_control_pins();
    void reset_all_pins();

    spi_device_handle_t device_ = nullptr;
    std::uint8_t* dma_buffer_ = nullptr;
    bool owns_bus_ = false;
};

}  // namespace pf_display
