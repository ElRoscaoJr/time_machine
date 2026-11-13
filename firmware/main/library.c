#include "library.h"
#include "calendar.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "jpeg_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static SemaphoreHandle_t lock, index_lock;
static photo_t *photos;
static size_t count;
static uint32_t generation;
static atomic_bool ready;

void photo_path(uint64_t id, const char *ext, char out[64])
{
    snprintf(out, 64, LIB_ROOT "/%02x/%016" PRIx64 ".%s", (unsigned)(id & 63), id, ext);
}

bool photo_id(const char *text, uint64_t *id)
{
    if (!text || strlen(text) != 16 || strspn(text, "0123456789abcdef") != 16) return false;
    *id = strtoull(text, NULL, 16);
    return *id != 0;
}

static int compare(const void *a, const void *b)
{
    const photo_t *x = a, *y = b;
    int dx = x->date ? x->date : INT32_MAX;
    int dy = y->date ? y->date : INT32_MAX;
    if (dx != dy) return dx < dy ? -1 : 1;
    return (x->id > y->id) - (x->id < y->id);
}

static bool valid_string(cJSON *obj, const char *key, size_t max)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return !value || (cJSON_IsString(value) && strlen(value->valuestring) <= max);
}

static bool valid_meta(cJSON *obj)
{
    if (!cJSON_IsObject(obj) || !valid_string(obj, "date", 10) ||
        !valid_string(obj, "place", 160) || !valid_string(obj, "note", NOTE_LIMIT)) return false;
    const char *date = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, "date"));
    if (date && *date && !date_parse(date)) return false;
    cJSON *lat = cJSON_GetObjectItemCaseSensitive(obj, "lat");
    cJSON *lon = cJSON_GetObjectItemCaseSensitive(obj, "lon");
    if ((!lat || cJSON_IsNull(lat)) && (!lon || cJSON_IsNull(lon))) return true;
    return cJSON_IsNumber(lat) && cJSON_IsNumber(lon) &&
        fabs(lat->valuedouble) <= 90 && fabs(lon->valuedouble) <= 180;
}

static cJSON *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buffer = malloc(JSON_LIMIT + 1);
    cJSON *doc = NULL;
    if (buffer) {
        size_t n = fread(buffer, 1, JSON_LIMIT + 1, f);
        if (!ferror(f) && n <= JSON_LIMIT && !memchr(buffer, 0, n)) {
            buffer[n] = 0;
            doc = cJSON_ParseWithLengthOpts(buffer, n + 1, NULL, true);
        }
        free(buffer);
    }
    fclose(f);
    return doc;
}

static cJSON *read_meta(uint64_t id, const char *ext, photo_t *photo)
{
    char path[64];
    photo_path(id, ext, path);
    cJSON *obj = read_file(path);
    uint64_t parsed;
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(obj, "rev");
    if (!valid_meta(obj) ||
        !photo_id(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj,"id")), &parsed) ||
        parsed != id || !cJSON_IsNumber(rev) || rev->valuedouble < 1 ||
        rev->valuedouble > UINT32_MAX || floor(rev->valuedouble) != rev->valuedouble) {
        cJSON_Delete(obj);
        return NULL;
    }
    if (photo) *photo = (photo_t){id,
        date_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj,"date"))),
        (uint32_t)rev->valuedouble};
    return obj;
}

static void cleanup_deleted(uint64_t id)
{
    char jpg[64], json[64], bak[64], deleted[64];
    photo_path(id, "jpg", jpg);
    photo_path(id, "json", json);
    photo_path(id, "bak", bak);
    photo_path(id, "del", deleted);
    if (unlink(jpg) && errno != ENOENT) return;
    if (unlink(json) && errno != ENOENT) return;
    if (unlink(bak) && errno != ENOENT) return;
    unlink(deleted);
}

bool library_init(void)
{
    lock = xSemaphoreCreateMutex();
    index_lock = xSemaphoreCreateMutex();
    if (!lock || !index_lock) return false;
    photos = heap_caps_malloc(PHOTO_LIMIT * sizeof(*photos), MALLOC_CAP_SPIRAM);
    if (!photos || (mkdir(LIB_ROOT, 0775) && errno != EEXIST)) return false;
    for (unsigned shard = 0; shard < 64; ++shard) {
        char dir[32];
        snprintf(dir, sizeof(dir), LIB_ROOT "/%02x", shard);
        if (mkdir(dir, 0775) && errno != EEXIST) return false;
        DIR *d = opendir(dir);
        if (!d) return false;
        struct dirent *entry;
        while ((entry = readdir(d))) {
            if (strlen(entry->d_name) != 20 || entry->d_name[16] != '.') continue;
            char id_text[17];
            memcpy(id_text, entry->d_name, 16);
            id_text[16] = 0;
            uint64_t id;
            if (!photo_id(id_text, &id) || (id & 63) != shard) continue;
            if (!strcmp(entry->d_name + 17, "del")) {
                cleanup_deleted(id);
                continue;
            }
            // Each JPEG owns one catalog entry. Metadata alone never publishes an image.
            if (strcmp(entry->d_name + 17, "jpg")) continue;
            char json[64], bak[64], deleted[64];
            photo_path(id, "json", json);
            photo_path(id, "bak", bak);
            photo_path(id, "del", deleted);
            if (!access(deleted, F_OK) || count == PHOTO_LIMIT) continue;
            photo_t photo;
            cJSON *meta = read_meta(id, "json", &photo);
            if (!meta) {
                meta = read_meta(id, "bak", &photo);
                if (meta) {
                    unlink(json);
                    rename(bak, json);
                }
            }
            if (meta) photos[count++] = photo;
            if ((count & 31) == 0) vTaskDelay(1);
            cJSON_Delete(meta);
        }
        closedir(d);
    }
    qsort(photos, count, sizeof(*photos), compare);
    generation = 1;
    atomic_store(&ready,true);
    return true;
}

bool library_ready(void) { return atomic_load(&ready); }

static bool take(void) { return atomic_load(&ready) && xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE; }

static bool take_index(void)
{
    return library_ready() && xSemaphoreTake(index_lock, portMAX_DELAY) == pdTRUE;
}

size_t library_count(void)
{
    if (!take_index()) return 0;
    size_t n = count;
    xSemaphoreGive(index_lock);
    return n;
}

uint32_t library_generation(void)
{
    if (!take_index()) return 0;
    uint32_t current = generation;
    xSemaphoreGive(index_lock);
    return current;
}

bool library_at(size_t pos, photo_t *out)
{
    if (!take_index()) return false;
    bool found = pos < count;
    if (found) *out = photos[pos];
    xSemaphoreGive(index_lock);
    return found;
}

static size_t find(uint64_t id)
{
    for (size_t i = 0; i < count; ++i) if (photos[i].id == id) return i;
    return count;
}

size_t library_position(uint64_t id, size_t fallback)
{
    if (!take_index()) return 0;
    size_t i = find(id);
    if (i == count) i = fallback < count ? fallback : (count ? count - 1 : 0);
    xSemaphoreGive(index_lock);
    return i;
}

static size_t first_undated(void)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (photos[mid].date) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

int library_last_date(void)
{
    if (!take_index()) return 0;
    size_t end = first_undated();
    int date = end ? photos[end-1].date : 0;
    xSemaphoreGive(index_lock);
    return date;
}

size_t library_nearest(int ordinal)
{
    if (!take_index()) return 0;
    size_t lo = 0, end = first_undated(), hi = end;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (date_day(photos[mid].date) < ordinal) lo = mid + 1;
        else hi = mid;
    }
    if (lo && (lo == end ||
        ordinal - date_day(photos[lo-1].date) <= date_day(photos[lo].date) - ordinal)) --lo;
    xSemaphoreGive(index_lock);
    return lo;
}

cJSON *library_read(uint64_t id)
{
    if (!take()) return NULL;
    cJSON *obj = NULL;
    if (find(id) < count) {
        obj = read_meta(id, "json", NULL);
        if (!obj) obj = read_meta(id, "bak", NULL);
    }
    xSemaphoreGive(lock);
    return obj;
}

static bool jpeg_dimensions(const uint8_t *data, size_t size, unsigned *w, unsigned *h)
{
    if (size < 4 || data[0] != 0xff || data[1] != 0xd8) return false;
    size_t i = 2;
    while (i < size && data[i++] == 0xff) {
        while (i < size && data[i] == 0xff) ++i;
        if (i + 3 > size) return false;
        unsigned marker = data[i++];
        unsigned length = (data[i] << 8) | data[i+1];
        if (length < 2 || length > size - i || marker == 0xda || marker == 0xd9) return false;
        if (marker == 0xc0) {
            if (length < 8 || data[i+2] != 8) return false;
            *h = (data[i+3] << 8) | data[i+4];
            *w = (data[i+5] << 8) | data[i+6];
            return *w && *w <= 480 && *h && *h <= 320;
        }
        i += length;
    }
    return false;
}

bool photo_decode(const char *path, void *pixels, unsigned *w, unsigned *h)
{
    struct stat st;
    if (stat(path, &st) || st.st_size < 4 || st.st_size > JPEG_LIMIT) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t *jpeg = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM);
    bool ok = jpeg && fread(jpeg, 1, st.st_size, file) == (size_t)st.st_size;
    fclose(file);
    if (ok) {
        esp_jpeg_image_cfg_t config = {
            .indata = jpeg, .indata_size = st.st_size, .outbuf = pixels,
            .outbuf_size = PIXELS * 2, .out_format = JPEG_IMAGE_FORMAT_RGB565,
        };
        esp_jpeg_image_output_t result;
        ok = jpeg_dimensions(jpeg, st.st_size, w, h) &&
            esp_jpeg_decode(&config, &result) == ESP_OK &&
            result.width == *w && result.height == *h;
    }
    free(jpeg);
    return ok;
}

static bool write_meta(uint64_t id, uint32_t rev, cJSON *source)
{
    char json[64], tmp[64], bak[64], text_id[17];
    photo_path(id, "json", json);
    photo_path(id, "tmp", tmp);
    photo_path(id, "bak", bak);
    snprintf(text_id, sizeof(text_id), "%016" PRIx64, id);
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return false;
    bool ok = cJSON_AddStringToObject(obj, "id", text_id) &&
        cJSON_AddNumberToObject(obj, "rev", rev);
    static const char *keys[] = {"date", "place", "lat", "lon", "note"};
    for (size_t i = 0; ok && i < sizeof(keys)/sizeof(*keys); ++i) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(source, keys[i]);
        if (!value) continue;
        cJSON *copy = cJSON_Duplicate(value, true);
        ok = copy && cJSON_AddItemToObject(obj, keys[i], copy);
        if (!ok) cJSON_Delete(copy);
    }
    char *encoded = ok ? cJSON_PrintUnformatted(obj) : NULL;
    cJSON_Delete(obj);
    ok = false;
    if (encoded && strlen(encoded) <= JSON_LIMIT) {
        FILE *f = fopen(tmp, "wb");
        if (f) {
            size_t n = strlen(encoded);
            ok = fwrite(encoded, 1, n, f) == n && !fflush(f) && !fsync(fileno(f));
            if (fclose(f)) ok = false;
        }
    }
    free(encoded);
    if (ok && !access(json, F_OK)) {
        if (unlink(bak) && errno != ENOENT) ok = false;
        if (ok && rename(json, bak)) ok = false;
    }
    if (ok && rename(tmp, json)) {
        if (access(json, F_OK)) rename(bak, json);
        ok = false;
    }
    if (!ok) unlink(tmp);
    return ok;
}

static int meta_date(cJSON *meta)
{
    return date_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta, "date")));
}

lib_result_t library_create(cJSON *meta, const char *staged, uint64_t *id)
{
    if (!valid_meta(meta)) return LIB_INVALID;
    unsigned w, h;
    void *pixels = heap_caps_malloc(PIXELS * 2, MALLOC_CAP_SPIRAM);
    if (!pixels) return LIB_IO;
    bool decoded = photo_decode(staged, pixels, &w, &h);
    free(pixels);
    if (!decoded) return LIB_INVALID;
    if (!take()) return LIB_IO;
    lib_result_t result = LIB_FULL;
    if (count < PHOTO_LIMIT) {
        char path[64], meta_path[64];
        do {
            esp_fill_random(id, sizeof(*id));
            photo_path(*id, "jpg", path);
            photo_path(*id, "json", meta_path);
        } while (!*id || !access(path, F_OK) || !access(meta_path, F_OK));
        result = LIB_IO;
        if (write_meta(*id, 1, meta)) {
            if (!rename(staged, path)) {
                xSemaphoreTake(index_lock, portMAX_DELAY);
                photo_t added = {*id, meta_date(meta), 1};
                size_t lo = 0, hi = count;
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    if (compare(&photos[mid], &added) < 0) lo = mid + 1;
                    else hi = mid;
                }
                memmove(photos+lo+1,photos+lo,(count-lo)*sizeof(*photos));
                photos[lo] = added;
                ++count;
                ++generation;
                xSemaphoreGive(index_lock);
                result = LIB_OK;
            } else unlink(meta_path);
        }
    }
    xSemaphoreGive(lock);
    return result;
}

static lib_result_t update(size_t pos, uint32_t rev, cJSON *meta)
{
    if (pos == count) return LIB_MISSING;
    if (photos[pos].rev != rev || rev == UINT32_MAX) return LIB_CONFLICT;
    if (!valid_meta(meta)) return LIB_INVALID;
    if (!write_meta(photos[pos].id, rev + 1, meta)) return LIB_IO;
    int date = meta_date(meta);
    xSemaphoreTake(index_lock, portMAX_DELAY);
    ++photos[pos].rev;
    if (photos[pos].date != date) {
        photos[pos].date = date;
        qsort(photos, count, sizeof(*photos), compare);
    }
    ++generation;
    xSemaphoreGive(index_lock);
    return LIB_OK;
}

lib_result_t library_edit(uint64_t id, uint32_t rev, cJSON *meta)
{
    if (!take()) return LIB_IO;
    lib_result_t result = update(find(id), rev, meta);
    xSemaphoreGive(lock);
    return result;
}

lib_result_t library_note(uint64_t id, uint32_t rev, const char *note)
{
    cJSON *meta = library_read(id);
    if (!meta) return LIB_MISSING;
    cJSON_DeleteItemFromObjectCaseSensitive(meta, "note");
    lib_result_t result = cJSON_AddStringToObject(meta, "note", note) ?
        library_edit(id, rev, meta) : LIB_IO;
    cJSON_Delete(meta);
    return result;
}

lib_result_t library_delete(uint64_t id, uint32_t rev)
{
    if (!take()) return LIB_IO;
    size_t pos = find(id);
    lib_result_t result = LIB_MISSING;
    if (pos < count) {
        result = LIB_CONFLICT;
        if (photos[pos].rev == rev) {
            char json[64], deleted[64];
            photo_path(id, "json", json);
            photo_path(id, "del", deleted);
            result = LIB_IO;
            if (!rename(json, deleted)) {
                xSemaphoreTake(index_lock, portMAX_DELAY);
                memmove(photos + pos, photos + pos + 1, (count - pos - 1)*sizeof(*photos));
                --count;
                ++generation;
                xSemaphoreGive(index_lock);
                cleanup_deleted(id);
                result = LIB_OK;
            }
        }
    }
    xSemaphoreGive(lock);
    return result;
}
