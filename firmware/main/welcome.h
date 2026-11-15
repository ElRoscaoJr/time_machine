#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t welcome_pending(bool *pending);
esp_err_t welcome_schedule(bool pending);
