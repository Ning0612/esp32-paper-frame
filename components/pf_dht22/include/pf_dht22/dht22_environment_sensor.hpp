#pragma once

#include "driver/gpio.h"
#include "pf_sensors/environment_sensor.hpp"

namespace pf_dht22 {

// Adapts the vendored dht.c/dht.h driver (see
// components/pf_dht22/third_party/, docs/adr/0006-sensor-drivers-and-presence.md)
// to pf_sensors::EnvironmentSensor. Blocking/bit-bang timing lives entirely
// in the vendored driver and is not touched here.
class Dht22EnvironmentSensor final : public pf_sensors::EnvironmentSensor {
public:
    explicit Dht22EnvironmentSensor(gpio_num_t pin);

    pf_sensors::SensorStatus probe() override;
    bool read(pf_sensors::EnvironmentReading& output) override;

private:
    gpio_num_t pin_;
};

}  // namespace pf_dht22
