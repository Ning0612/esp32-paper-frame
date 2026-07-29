#pragma once

#include "esp_err.h"
#include "pf_config/schema.hpp"

namespace pf_config {

struct StartupResult {
    SchemaAction action;
    esp_err_t error;
};

StartupResult initialize();

}  // namespace pf_config
