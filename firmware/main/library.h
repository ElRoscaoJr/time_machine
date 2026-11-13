#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cJSON.h"

#define PHOTO_LIMIT 12000
#define JPEG_LIMIT (192 * 1024)
#define JSON_LIMIT 16384
#define NOTE_LIMIT 4096
#define PIXELS (480 * 320)
#define SD_ROOT "/sd"
#define LIB_ROOT SD_ROOT "/entretiempo"

typedef struct {
    uint64_t id;
    int date;
    uint32_t rev;
} photo_t;

typedef enum { LIB_OK, LIB_INVALID, LIB_MISSING, LIB_CONFLICT, LIB_IO, LIB_FULL } lib_result_t;

bool library_init(void);
bool library_ready(void);
size_t library_count(void);
uint32_t library_generation(void);
bool library_at(size_t pos, photo_t *out);
size_t library_position(uint64_t id, size_t fallback);
int library_last_date(void);
size_t library_nearest(int ordinal);
cJSON *library_read(uint64_t id);
void photo_path(uint64_t id, const char *extension, char out[64]);
bool photo_id(const char *text, uint64_t *id);
bool photo_decode(const char *path, void *pixels, unsigned *w, unsigned *h);
lib_result_t library_create(cJSON *meta, const char *staged, uint64_t *id);
lib_result_t library_edit(uint64_t id, uint32_t rev, cJSON *meta);
lib_result_t library_note(uint64_t id, uint32_t rev, const char *note);
lib_result_t library_delete(uint64_t id, uint32_t rev);
