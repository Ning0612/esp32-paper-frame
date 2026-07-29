#include "pf_display/epd7in3e_esp_idf.hpp"

#include <cstring>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pf_display {
namespace {

constexpr spi_host_device_t kPanelSpiHost = SPI2_HOST;
constexpr gpio_num_t kMosiPin = static_cast<gpio_num_t>(kPanelMosiGpio);
constexpr gpio_num_t kClockPin = static_cast<gpio_num_t>(kPanelClockGpio);
constexpr gpio_num_t kChipSelectPin =
    static_cast<gpio_num_t>(kPanelChipSelectGpio);
constexpr gpio_num_t kDataCommandPin =
    static_cast<gpio_num_t>(kPanelDataCommandGpio);
constexpr gpio_num_t kResetPin = static_cast<gpio_num_t>(kPanelResetGpio);
constexpr gpio_num_t kBusyPin = static_cast<gpio_num_t>(kPanelBusyGpio);

}  // namespace

EspIdfPanelTransport::~EspIdfPanelTransport()
{
    shutdown();
}

esp_err_t EspIdfPanelTransport::initialize()
{
    if (initialized() || owns_bus_ || dma_buffer_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    gpio_config_t output_config{};
    output_config.pin_bit_mask =
        (1ULL << kPanelDataCommandGpio) | (1ULL << kPanelResetGpio);
    output_config.mode = GPIO_MODE_OUTPUT;
    output_config.pull_up_en = GPIO_PULLUP_DISABLE;
    output_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    output_config.intr_type = GPIO_INTR_DISABLE;
    esp_err_t result = gpio_config(&output_config);
    if (result != ESP_OK) {
        return result;
    }

    gpio_config_t input_config{};
    input_config.pin_bit_mask = 1ULL << kPanelBusyGpio;
    input_config.mode = GPIO_MODE_INPUT;
    input_config.pull_up_en = GPIO_PULLUP_DISABLE;
    input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input_config.intr_type = GPIO_INTR_DISABLE;
    result = gpio_config(&input_config);
    if (result != ESP_OK) {
        reset_control_pins();
        return result;
    }

    if (gpio_set_level(kResetPin, 1) != ESP_OK ||
        gpio_set_level(kDataCommandPin, 0) != ESP_OK) {
        reset_control_pins();
        return ESP_FAIL;
    }

    spi_bus_config_t bus_config{};
    bus_config.mosi_io_num = kMosiPin;
    bus_config.miso_io_num = -1;
    bus_config.sclk_io_num = kClockPin;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.data4_io_num = -1;
    bus_config.data5_io_num = -1;
    bus_config.data6_io_num = -1;
    bus_config.data7_io_num = -1;
    bus_config.max_transfer_sz =
        static_cast<int>(kPanelTransactionBytes);
    result =
        spi_bus_initialize(kPanelSpiHost, &bus_config, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) {
        reset_control_pins();
        return result;
    }
    owns_bus_ = true;

    spi_device_interface_config_t device_config{};
    device_config.clock_speed_hz = static_cast<int>(kPanelSpiClockHz);
    device_config.mode = 0;
    device_config.spics_io_num = kChipSelectPin;
    device_config.queue_size = 1;
    result =
        spi_bus_add_device(kPanelSpiHost, &device_config, &device_);
    if (result != ESP_OK) {
        shutdown();
        return result;
    }

    dma_buffer_ = static_cast<std::uint8_t*>(heap_caps_malloc(
        kPanelTransactionBytes,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (dma_buffer_ == nullptr) {
        shutdown();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t EspIdfPanelTransport::shutdown()
{
    esp_err_t first_error = ESP_OK;

    if (device_ != nullptr) {
        const esp_err_t result = spi_bus_remove_device(device_);
        if (result != ESP_OK) {
            first_error = result;
        } else {
            device_ = nullptr;
        }
    }

    if (owns_bus_ && device_ == nullptr) {
        const esp_err_t result = spi_bus_free(kPanelSpiHost);
        if (result != ESP_OK && first_error == ESP_OK) {
            first_error = result;
        } else if (result == ESP_OK) {
            owns_bus_ = false;
        }
    }

    if (dma_buffer_ != nullptr && device_ == nullptr) {
        heap_caps_free(dma_buffer_);
        dma_buffer_ = nullptr;
    }

    if (device_ == nullptr && !owns_bus_) {
        reset_all_pins();
    }
    return first_error;
}

bool EspIdfPanelTransport::initialized() const
{
    return device_ != nullptr && owns_bus_ && dma_buffer_ != nullptr;
}

bool EspIdfPanelTransport::set_reset_level(const bool high)
{
    return initialized() &&
           gpio_set_level(kResetPin, high ? 1 : 0) == ESP_OK;
}

bool EspIdfPanelTransport::write_command_transaction(
    const std::uint8_t command)
{
    return initialized() &&
           gpio_set_level(kDataCommandPin, 0) == ESP_OK &&
           transmit_command(command);
}

bool EspIdfPanelTransport::write_data_transaction(
    const std::uint8_t* const data,
    const std::size_t length)
{
    if (!initialized() || data == nullptr || length == 0U ||
        length > kPanelTransactionBytes) {
        return false;
    }

    std::memcpy(dma_buffer_, data, length);
    return gpio_set_level(kDataCommandPin, 1) == ESP_OK &&
           transmit_data(length);
}

bool EspIdfPanelTransport::busy_is_idle(bool& idle)
{
    if (!initialized()) {
        return false;
    }
    idle = gpio_get_level(kBusyPin) != 0;
    return true;
}

std::uint64_t EspIdfPanelTransport::monotonic_ms() const
{
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
}

void EspIdfPanelTransport::delay_ms(const std::uint32_t duration_ms)
{
    const std::uint64_t converted = detail::ticks_to_wait_at_least(
        duration_ms,
        portTICK_PERIOD_MS);
    if (converted > 0U) {
        vTaskDelay(static_cast<TickType_t>(converted));
    }
}

bool EspIdfPanelTransport::transmit_command(const std::uint8_t command)
{
    spi_transaction_t transaction{};
    transaction.flags = SPI_TRANS_USE_TXDATA;
    transaction.length = 8U;
    transaction.tx_data[0] = command;
    return spi_device_polling_transmit(device_, &transaction) == ESP_OK;
}

bool EspIdfPanelTransport::transmit_data(const std::size_t length)
{
    spi_transaction_t transaction{};
    transaction.length = length * 8U;
    transaction.tx_buffer = dma_buffer_;
    return spi_device_polling_transmit(device_, &transaction) == ESP_OK;
}

void EspIdfPanelTransport::reset_control_pins()
{
    gpio_reset_pin(kBusyPin);
    gpio_reset_pin(kResetPin);
    gpio_reset_pin(kDataCommandPin);
}

void EspIdfPanelTransport::reset_all_pins()
{
    reset_control_pins();
    gpio_reset_pin(kChipSelectPin);
    gpio_reset_pin(kClockPin);
    gpio_reset_pin(kMosiPin);
}

}  // namespace pf_display
