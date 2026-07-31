#include "pf_dht22/dht22_environment_sensor.hpp"

#include "dht.h"

namespace pf_dht22 {

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
    pf_sensors::EnvironmentReading& output)
{
    float humidity = 0.0F;
    float temperature = 0.0F;
    const esp_err_t result = dht_read_float_data(
        DHT_TYPE_AM2301, pin, &humidity, &temperature);
    if (result != ESP_OK) {
        return false;
    }
    output = pf_sensors::EnvironmentReading{temperature, humidity};
    return true;
}

}  // namespace

pf_sensors::SensorStatus Dht22EnvironmentSensor::probe()
{
    pf_sensors::EnvironmentReading reading{};
    const bool ok = read_raw(pin_, reading);
    return pf_sensors::classify_environment_read(ok, reading);
}

bool Dht22EnvironmentSensor::read(pf_sensors::EnvironmentReading& output)
{
    pf_sensors::EnvironmentReading reading{};
    if (!read_raw(pin_, reading) ||
        !pf_sensors::environment_reading_in_range(reading)) {
        return false;
    }
    output = reading;
    return true;
}

}  // namespace pf_dht22
