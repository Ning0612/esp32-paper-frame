#pragma once

#include "driver/gpio.h"
#include "pf_sensors/environment_sensor.hpp"

namespace pf_sensors {

// Adapts the vendored dht.c/dht.h driver (see
// components/pf_sensors/third_party/, docs/adr/0006-sensor-drivers-and-presence.md)
// to pf_sensors::EnvironmentSensor. Blocking/bit-bang timing lives entirely
// in the vendored driver and is not touched here.
class Dht22EnvironmentSensor final : public EnvironmentSensor {
public:
    explicit Dht22EnvironmentSensor(gpio_num_t pin);

    SensorStatus probe() override;
    bool read(EnvironmentReading& output) override;

private:
    gpio_num_t pin_;
};

}  // namespace pf_sensors
