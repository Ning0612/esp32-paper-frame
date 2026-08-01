#include "pf_sensors/dht22_environment_sensor.hpp"

#include "dht.h"

namespace pf_sensors {

Dht22EnvironmentSensor::Dht22EnvironmentSensor(const gpio_num_t pin)
    : pin_(pin)
{
}

namespace {

// Raw driver read, not range-filtered: lets probe() distinguish "sensor
// didn't answer" (not_detected) from "sensor answered but the value is
// out of the DHT22 datasheet range" (error) via classify_environment_read.
// read() below still only reports success for in-range data.
bool read_raw(
    const gpio_num_t pin,
    EnvironmentReading& output)
{
    float humidity = 0.0F;
    float temperature = 0.0F;
    const esp_err_t result = dht_read_float_data(
        DHT_TYPE_AM2301, pin, &humidity, &temperature);
    if (result != ESP_OK) {
        return false;
    }
    output = EnvironmentReading{temperature, humidity};
    return true;
}

}  // namespace

SensorStatus Dht22EnvironmentSensor::probe()
{
    EnvironmentReading reading{};
    const bool ok = read_raw(pin_, reading);
    return classify_environment_read(ok, reading);
}

bool Dht22EnvironmentSensor::read(EnvironmentReading& output)
{
    EnvironmentReading reading{};
    if (!read_raw(pin_, reading) ||
        !environment_reading_in_range(reading)) {
        return false;
    }
    output = reading;
    return true;
}

}  // namespace pf_sensors
