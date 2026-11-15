#include "locality.h"
#include "esp_heap_caps.h"
#include "miniz.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t localidades_start[] asm("_binary_localidades_tsv_gz_start");
extern const uint8_t localidades_end[] asm("_binary_localidades_tsv_gz_end");

typedef struct {
    float latitude, longitude;
    const char *name;
} locality_t;

static locality_t *catalog;
static size_t catalog_count;

void locality_init(void)
{
    // build.mjs generates a gzip stream with a fixed ten-byte header.
    const uint8_t *footer = localidades_end - 8;
    size_t size = (uint32_t)footer[4] | ((uint32_t)footer[5] << 8) |
        ((uint32_t)footer[6] << 16) | ((uint32_t)footer[7] << 24);
    char *text = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    tinfl_decompressor *decoder = malloc(sizeof(*decoder));
    if (!text || !decoder) {
        free(text);
        free(decoder);
        return;
    }
    size_t compressed = footer - (localidades_start + 10);
    size_t output = size;
    tinfl_init(decoder);
    tinfl_status status = tinfl_decompress(decoder, localidades_start + 10, &compressed,
        (uint8_t *)text, (uint8_t *)text, &output, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(decoder);
    if (status != TINFL_STATUS_DONE || output != size) {
        free(text);
        return;
    }
    text[size] = '\0';
    char *line = strchr(text, '\n');
    size_t rows = 0;
    for (char *end = line; end; end = strchr(end + 1, '\n')) ++rows;
    if (rows) --rows;
    catalog = heap_caps_malloc(rows * sizeof(*catalog), MALLOC_CAP_SPIRAM);
    if (!catalog) {
        free(text);
        return;
    }
    while (catalog_count < rows) {
        ++line;
        char *end = strchr(line, '\n');
        *end = '\0';
        char *longitude, *name;
        float lat = strtof(line, &longitude);
        if (*longitude != '\t') break;
        float lon = strtof(longitude + 1, &name);
        if (*name != '\t') break;
        catalog[catalog_count++] = (locality_t){lat, lon, name + 1};
        line = end;
    }
}

const char *locality_nearest(double latitude, double longitude)
{
    const double radius = 12.0 / 111.2;
    size_t first = 0, last = catalog_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (catalog[middle].latitude < latitude - radius) first = middle + 1;
        else last = middle;
    }
    double longitude_scale = cos(latitude * (3.141592653589793 / 180.0));
    double best = radius * radius;
    const char *name = NULL;
    for (size_t i = first; i < catalog_count && catalog[i].latitude <= latitude + radius; ++i) {
        double dy = catalog[i].latitude - latitude;
        double dx = (catalog[i].longitude - longitude) * longitude_scale;
        double distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            name = catalog[i].name;
        }
    }
    return name;
}
