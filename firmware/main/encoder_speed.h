#pragma once

#include <stdint.h>

typedef struct {
    int direction;
    int64_t last_step_ms;
    unsigned remainder;
} encoder_speed_t;

/* Return the number of photos for one physical detent. The fractional
 * part accumulates across steps so acceleration builds without hard jumps. */
static inline int encoder_photo_step(encoder_speed_t *speed, int direction, int64_t now_ms)
{
    enum { ONE_PHOTO = 256, SLOW_MS = 100, FAST_MS = 16, MAX_STEP = 16 };
    int64_t interval = now_ms - speed->last_step_ms;
    int previous_direction = speed->direction;
    speed->direction = direction;
    speed->last_step_ms = now_ms;

    if (direction != previous_direction || interval >= SLOW_MS) {
        speed->remainder = 0;
        return 1;
    }

    int span = SLOW_MS - FAST_MS;
    int pace = interval <= FAST_MS ? span : SLOW_MS - (int)interval;
    unsigned extra = (MAX_STEP - 1) * ONE_PHOTO * pace * pace / (span * span);
    unsigned total = ONE_PHOTO + extra + speed->remainder;
    speed->remainder = total % ONE_PHOTO;
    return (int)(total / ONE_PHOTO);
}
