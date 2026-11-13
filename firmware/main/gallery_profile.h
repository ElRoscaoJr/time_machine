#pragma once
#include <stdint.h>

#define PROFILE_IMAGE_SIZE 64
#define PROFILE_IMAGE_BYTES (PROFILE_IMAGE_SIZE * PROFILE_IMAGE_SIZE * 2)

void gallery_profile_set(const uint8_t *pixels);
