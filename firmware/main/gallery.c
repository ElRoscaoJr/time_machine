#include "board.h"
#include "calendar.h"
#include "config.h"
#include "encoder_speed.h"
#include "library.h"
#include "gallery_profile.h"
#include "network.h"
#include "welcome.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(font_es_14);

#ifdef HAS_PROFILE_AVATAR
extern const uint8_t avatar_pixels[] asm("_binary_profile_avatar_rgb565_start");
static const lv_image_dsc_t profile_avatar = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = 72, .h = 72, .stride = 144},
    .data_size = 72 * 72 * 2,
    .data = avatar_pixels,
};
#endif

static uint16_t custom_pixels[PROFILE_IMAGE_SIZE * PROFILE_IMAGE_SIZE];
static const lv_image_dsc_t custom_avatar = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = PROFILE_IMAGE_SIZE, .h = PROFILE_IMAGE_SIZE, .stride = PROFILE_IMAGE_SIZE * 2},
    .data_size = PROFILE_IMAGE_BYTES,
    .data = (const uint8_t *)custom_pixels,
};
static lv_obj_t *avatar_frame, *avatar_image;

#define DEVICE_TITLE "Daniela's Time Machine"
#define PAPER 0xece1cb
#define BRASS 0xa68a56
#define AMBER 0xffbd62
#define COAL 0x141311
#define PHOTO_STAGE_HEIGHT 244
#define FOOTER_BUTTON_Y (PHOTO_STAGE_HEIGHT + 3)
#define FOOTER_BUTTON_HEIGHT 30
#define TRACK 424
#define TRACK_INSET (MARKER_SIZE / 2)
#define MARKER_SIZE 10
#define TURN_MS 144
#define CACHE_SIZE 3
#define NOTE_PREVIEW_BYTES 192
#define LOCATION_TEXT_BYTES 192

typedef struct {
    lv_image_dsc_t image;
    uint64_t id;
    uint32_t rev;
    char location[LOCATION_TEXT_BYTES];
    char note_preview[NOTE_PREVIEW_BYTES];
} frame_t;

typedef struct {
    uint64_t id, next, other;
    uint32_t rev;
} load_t;

static frame_t frames[CACHE_SIZE];
static QueueHandle_t loads;
static int showing = -1;
static int turn_target = -1;
static int turn_direction;
static bool turning, turned;
static lv_obj_t *home, *picture, *caption, *age_label, *track, *cursor;
static lv_font_t spanish_title_font;
static lv_obj_t *end_label, *start_label, *placeholder, *welcome, *paper_edge, *track_fill;
static lv_obj_t *photo_location, *photo_note, *date_button;
static lv_obj_t *sheet, *editor, *keyboard, *scroll, *note_status;
static lv_obj_t *date_field, *place_field, *panel_help, *panel_qr, *panel_url, *panel_hotspot;
static lv_timer_t *panel_timer;
static lv_obj_t *intro_overlay, *intro_title, *intro_body, *intro_next;
static size_t intro_step;
static const struct {
    const char *title, *body;
} intro_pages[] = {
    {"Hola Dani ;)", "Esta es tu \"máquina del tiempo\".\nLa he hecho para ti, que sé que te gusta guardarlo todo... Ahora puedes guardar recuerdos también"},
    {"Hueco para todo.", "Aquí caben amigos, familia, animales, lugares, emociones y, claro, también yo... Lo que tú quieras ;)"},
    {"Gira la ruedita", "Cada vuelta te lleva por tus fotos. También puedes tocar la línea de abajo para saltar a otro momento."},
    {"Lo que no sale en la foto", "Toca una foto o pulsa la ruedita para abrir su nota. Ahí puedes guardar lo que pasó justo antes, lo que dijiste o lo que te hizo reír."},
    {"Añadir o borrar fotos", "conectate a la wifi " AP_NAME ", contraseña \"" AP_PASSWORD
        "\" jeje, y te llevará a un panel de control para agregar, borrar o editar imágenes."},
    {"Todavía queda mucho por vivir", "Ojalá que, cada vez que vuelvas por aquí, encuentres algo que te haga sonreír.\n\nCon mucho amor, Samu :)"},
};
static photo_t selected, note_photo;
static size_t position;
static uint32_t last_generation;
static int first_day, last_day, last_today, direction = 1;
static int64_t last_turn;
static bool pending, clock_drawn, was_loading = true;
static bool follow_timeline_start = true;
static uint64_t failed;
static const char *lower_keys[] = {
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","ñ","\n",
    "ABC","z","x","c","v","b","n","m",LV_SYMBOL_BACKSPACE,"\n",
    "á","é","í","ó","ú","ü","¿","?","¡","!","\n",
    "1#",","," ",".","Enter",""
};
static const char *upper_keys[] = {
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","Ñ","\n",
    "abc","Z","X","C","V","B","N","M",LV_SYMBOL_BACKSPACE,"\n",
    "Á","É","Í","Ó","Ú","Ü","¿","?","¡","!","\n",
    "1#",","," ",".","Enter",""
};
static const lv_buttonmatrix_ctrl_t letter_widths[] = {
    1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,
    2,1,1,1,1,1,1,1,2, 1,1,1,1,1,1,1,1,1,1, 2,1,5,1,2
};
static const char *symbol_keys[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "!","¡","?","¿","(",")","[","]","\n",
    "\"","'","«","»","@",":",";","/","\n",
    "+","-","=","_","#","%","&",LV_SYMBOL_BACKSPACE,"\n",
    "abc",","," ",".","Enter",""
};
static const lv_buttonmatrix_ctrl_t symbol_widths[] = {
    1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,2, 2,1,5,1,2
};
static const char *date_keys[] = {
    "1","2","3","\n", "4","5","6","\n", "7","8","9","\n",
    "/","0",LV_SYMBOL_BACKSPACE,""
};
static const lv_buttonmatrix_ctrl_t date_widths[] = {1,1,1, 1,1,1, 1,1,1, 1,1,1};

static void edit_details(lv_event_t *event);

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);
    lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const char *text, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l,x,y);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &font_es_14, 0);
    lv_label_set_text(l,text);
    return l;
}

static int cached(uint64_t id)
{
    for (int i = 0; i < CACHE_SIZE; ++i)
        if (frames[i].id == id && id) return i;
    return -1;
}

static void render_photo(void);

static void location_text(const cJSON *meta, char text[LOCATION_TEXT_BYTES])
{
    const char *place = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta, "place"));
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(meta, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(meta, "lon");
    if (!place) place = "";
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        snprintf(text, LOCATION_TEXT_BYTES, "%.4f, %.4f%s%s",
            lat->valuedouble, lon->valuedouble, *place ? " · " : "", place);
    } else {
        snprintf(text, LOCATION_TEXT_BYTES, "%s", place);
    }
}

static void note_preview_text(const cJSON *meta, char text[NOTE_PREVIEW_BYTES])
{
    const char *note = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta, "note"));
    size_t length = 0;
    if (note) {
        note += strspn(note, " \t\r\n");
        while (*note && length < NOTE_PREVIEW_BYTES - 4) {
            if (strchr(" \t\r\n", *note)) {
                note += strspn(note, " \t\r\n");
                if (*note) text[length++] = ' ';
            } else text[length++] = *note++;
        }
        if (*note) {
            while (length && ((unsigned char)*note & 0xc0) == 0x80) { --length; --note; }
            memcpy(text + length, "...", 3);
            length += 3;
        }
    }
    text[length] = '\0';
}

static void show_caption(lv_obj_t *caption_label, const char *text)
{
    lv_label_set_text(caption_label, text);
    if (*text) lv_obj_remove_flag(caption_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(caption_label, LV_OBJ_FLAG_HIDDEN);
}

static void show_photo_details(const frame_t *frame)
{
    show_caption(photo_location, frame ? (*frame->location ? frame->location : "+ Añadir ubicación") : "");
    show_caption(photo_note, frame ? frame->note_preview : "");
}

static unsigned photo_scale(const frame_t *frame)
{
    unsigned height = frame->image.header.h;
    return height > PHOTO_STAGE_HEIGHT ? PHOTO_STAGE_HEIGHT * 256 / height : 256;
}

static void show_frame(int slot)
{
    showing = slot;
    lv_image_set_src(picture, &frames[slot].image);
    lv_image_set_pivot(picture, frames[slot].image.header.w/2, frames[slot].image.header.h/2);
    lv_image_set_scale(picture, photo_scale(&frames[slot]));
    lv_obj_center(picture);
    lv_obj_add_flag(welcome, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(picture, LV_OBJ_FLAG_HIDDEN);
    show_photo_details(&frames[slot]);
}

/* One RGB565 image is drawn at a time. Changing just the vertical scale
 * suggests a card pivoting around a horizontal axle without an alpha layer. */
static void turn_frame(void *object, int32_t progress)
{
    (void)object;
    if (progress >= 128 && !turned) {
        int next = !pending ? cached(selected.id) : -1;
        if (next >= 0) show_frame(next);
        else if (turn_target >= 0)
            show_frame(turn_target);
        turn_target = -1;
        turned = true;
    }
    int distance = progress < 128 ? progress : 256 - progress;
    unsigned fitted_scale = photo_scale(&frames[showing]);
    unsigned scale = fitted_scale * (256 - distance * 240 / 128) / 256;
    lv_image_set_scale_y(picture, scale);
    int offset = -turn_direction * distance / 10;
    lv_obj_align(picture, LV_ALIGN_CENTER, 0, offset);
    lv_obj_align(paper_edge, LV_ALIGN_CENTER, 0, offset);
    if (distance >= 100) {
        lv_obj_set_width(paper_edge, frames[showing].image.header.w * fitted_scale / 256);
        lv_obj_remove_flag(paper_edge, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(paper_edge, LV_OBJ_FLAG_HIDDEN);
}

static void finish_turn(lv_anim_t *anim)
{
    (void)anim;
    turning = false;
    turn_target = -1;
    if (!pending) render_photo();
}

static void render_photo(void)
{
    if (turning) return;
    int i = cached(selected.id);
    if (i < 0) {
        if (selected.id && failed != selected.id) return;
        showing = -1;
        lv_obj_add_flag(picture, LV_OBJ_FLAG_HIDDEN);
        show_photo_details(NULL);
        lv_label_set_text(placeholder,
            selected.id ? "No se pudo abrir esta foto" :
            library_loading() ? "Leyendo la microSD…" :
            library_ready() ? "Añade tus primeras fotos" : "Inserta una microSD FAT32 y reinicia");
        lv_obj_remove_flag(welcome, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (showing == i) return;
    if (showing < 0 || sheet) { show_frame(i); return; }

    turning = true;
    turned = false;
    turn_target = i;
    turn_direction = direction;
    lv_anim_t turn;
    lv_anim_init(&turn);
    lv_anim_set_var(&turn, picture);
    lv_anim_set_exec_cb(&turn, turn_frame);
    lv_anim_set_values(&turn, 0, 256);
    lv_anim_set_duration(&turn, TURN_MS);
    lv_anim_set_completed_cb(&turn, finish_turn);
    lv_anim_start(&turn);
}

static void request_photo(void)
{
    if (!selected.id) { render_photo(); return; }
    photo_t neighbor;
    load_t load = {.id = selected.id, .rev = selected.rev};
    if ((direction > 0 || position > 0) && library_at(position + direction, &neighbor))
        load.next = neighbor.id;
    if ((direction < 0 || position > 0) && library_at(position - direction, &neighbor))
        load.other = neighbor.id;
    render_photo();
    if (loads) xQueueOverwrite(loads, &load);
}

static void loader(void *arg)
{
    (void)arg;
    load_t load;
    while (xQueueReceive(loads, &load, portMAX_DELAY) == pdTRUE) {
        uint64_t ids[] = {load.id, load.next, load.other};
        for (int n = 0; n < CACHE_SIZE && !uxQueueMessagesWaiting(loads); ++n) {
            if (!ids[n]) continue;
            lvgl_port_lock(0);
            int slot = cached(ids[n]);
            bool decode = slot < 0;
            if (!decode && (n || frames[slot].rev == load.rev)) slot = -1;
            if (decode && failed != ids[n]) {
                for (int i = 0; i < CACHE_SIZE; ++i) {
                    if (i == showing || i == turn_target) continue;
                    uint64_t id = frames[i].id;
                    if (n && id && (id == load.id || id == load.next || id == load.other)) continue;
                    if (!frames[i].image.data) continue;
                    slot = i;
                    frames[i].id = 0;
                    lv_image_cache_drop(&frames[i].image);
                    lv_image_header_cache_drop(&frames[i].image);
                    break;
                }
            }
            lvgl_port_unlock();
            if (slot < 0) continue;
            unsigned w = 0, h = 0;
            bool ok = true;
            if (decode) {
                char path[64];
                photo_path(ids[n], "jpg", path);
                ok = photo_decode(path, (void *)frames[slot].image.data, &w, &h);
            }
            cJSON *meta = ok ? library_read(ids[n]) : NULL;
            const cJSON *rev = cJSON_GetObjectItemCaseSensitive(meta, "rev");
            char location[LOCATION_TEXT_BYTES];
            location_text(meta, location);
            char note_preview[NOTE_PREVIEW_BYTES];
            note_preview_text(meta, note_preview);
            lvgl_port_lock(0);
            if (ok) {
                frame_t *frame = &frames[slot];
                if (decode) {
                    frame->image.header.w = w;
                    frame->image.header.h = h;
                    frame->image.header.stride = w*2;
                    frame->image.data_size = w*h*2;
                }
                frame->id = ids[n];
                frame->rev = rev ? (uint32_t)rev->valuedouble : 0;
                strcpy(frame->location, location);
                strcpy(frame->note_preview, note_preview);
                if (slot == showing) show_photo_details(frame);
            } else if (ids[n] == selected.id) failed = ids[n];
            if (!pending && ids[n] == selected.id) render_photo();
            lvgl_port_unlock();
            cJSON_Delete(meta);
            taskYIELD();
        }
    }
}

static int track_x(int day)
{
    if (day <= first_day) return 0;
    if (day >= last_day) return TRACK;
    return (int)((int64_t)(day - first_day) * TRACK / (last_day - first_day));
}

static void timeline(int today)
{
    int start = PROFILE_BIRTH ? PROFILE_BIRTH : 20000101;
    int newest = library_last_date();
    int end = LV_MAX(today, newest);
    end = LV_MAX(end, start);
    first_day = date_day(start);
    last_day = date_day(end);
    if (last_day <= first_day) last_day = first_day + 1;
    lv_obj_clean(track);
    for (int y = start/10000; y <= end/10000 && y - start/10000 <= 120; ++y) {
        int tick = date_day(y*10000 + (PROFILE_BIRTH ? PROFILE_BIRTH%10000 : 101));
        if (tick < first_day || tick > last_day) continue;
        int x = TRACK_INSET + track_x(tick);
        bool milestone = (y - start/10000)%5 == 0;
        lv_obj_remove_flag(box(track, x, 8, 1, milestone ? 13 : 9, BRASS), LV_OBJ_FLAG_CLICKABLE);
        if (milestone && x > 42 && x < TRACK - 42) {
            lv_obj_t *year = label(track,x-20,23,"",BRASS);
            lv_obj_set_width(year,40);
            lv_obj_set_style_text_align(year,LV_TEXT_ALIGN_CENTER,0);
            lv_label_set_text_fmt(year,"%d",y);
        }
    }
    lv_obj_t *first_tick = box(track, TRACK_INSET, 15, TRACK + 1, 3, 0x6b5737);
    lv_obj_t *last_tick = box(track, TRACK_INSET + TRACK, 8, 1, 13, BRASS);
    track_fill = box(track, TRACK_INSET, 15, 1, 3, AMBER);
    lv_obj_remove_flag(track_fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(first_tick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(last_tick, LV_OBJ_FLAG_CLICKABLE);
    cursor = box(track, 0, 11, MARKER_SIZE, MARKER_SIZE, AMBER);
    lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cursor, 1, 0);
    lv_obj_set_style_border_color(cursor, lv_color_hex(0xffdf9d), 0);
    lv_obj_remove_flag(cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_fmt(start_label, "%d", start/10000);
    lv_label_set_text_fmt(end_label, "%d", end/10000);
}

static void cursor_position(void *object, int32_t x)
{
    lv_obj_set_x(object,x);
    lv_obj_set_width(track_fill,x+1);
}

static void update_labels(void)
{
    if (selected.id) lv_obj_remove_flag(date_button, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(date_button, LV_OBJ_FLAG_HIDDEN);
    int today = date_today();
    if (!clock_drawn || today != last_today) {
        timeline(today);
        last_today = today;
        clock_drawn = true;
    }
    if (selected.date) {
        lv_label_set_text_fmt(caption, "%02d/%02d/%04d", selected.date%100, selected.date/100%100, selected.date/10000);
        int x = track_x(date_day(selected.date));
        if (lv_obj_has_flag(cursor,LV_OBJ_FLAG_HIDDEN)) {
            lv_anim_delete(cursor,cursor_position);
            cursor_position(cursor,x);
        } else if (lv_obj_get_x(cursor) != x) {
            lv_anim_t slide;
            lv_anim_init(&slide);
            lv_anim_set_var(&slide,cursor);
            lv_anim_set_exec_cb(&slide,cursor_position);
            lv_anim_set_values(&slide,lv_obj_get_x(cursor),x);
            lv_anim_set_duration(&slide,pending ? 55 : 115);
            lv_anim_set_path_cb(&slide,lv_anim_path_ease_out);
            lv_anim_start(&slide);
        }
        lv_obj_remove_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(track_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(caption, selected.id ? "+ Añadir fecha" : "");
        lv_anim_delete(cursor,cursor_position);
        lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(track_fill, LV_OBJ_FLAG_HIDDEN);
    }
    int age = date_age(PROFILE_BIRTH, selected.date);
    if (age >= 0) lv_label_set_text_fmt(age_label, "%s · %d años", PROFILE_NAME, age);
    else lv_label_set_text(age_label, PROFILE_NAME);
}

static void choose(size_t index, bool defer)
{
    position = index;
    if (!library_at(index, &selected)) selected = (photo_t){0};
    failed = 0;
    pending = defer;
    update_labels();
    if (!pending) request_photo();
}

static void jump(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_t *input = lv_indev_active();
    if (!input || !library_count()) return;
    lv_indev_get_point(input, &point);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    int x = LV_CLAMP(0, point.x - area.x1 - TRACK_INSET, TRACK);
    size_t target = library_nearest(first_day + (int)((int64_t)x*(last_day-first_day)/TRACK));
    follow_timeline_start = false;
    direction = target < position ? -1 : 1;
    choose(target, false);
}

static void close_note(lv_event_t *event)
{
    (void)event;
    if (!sheet) return;
    if (panel_timer) {
        lv_timer_delete(panel_timer);
        panel_timer = NULL;
    }
    lv_obj_delete(sheet);
    sheet = editor = keyboard = scroll = note_status = date_field = place_field = NULL;
    note_photo = (photo_t){0};
}

static void save_note(lv_event_t *event)
{
    (void)event;
    lib_result_t result = library_note(note_photo.id, note_photo.rev, lv_textarea_get_text(editor));
    if (result == LIB_OK) close_note(NULL);
    else lv_label_set_text(note_status, result == LIB_CONFLICT ?
        "La nota cambió. Copia tu texto y vuelve a abrirla." : "No se pudo guardar la nota.");
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, const char *text, lv_event_cb_t action)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b,x,y);
    lv_obj_set_size(b,width,32);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x4c3e29),0);
    lv_obj_t *l = label(b,0,0,text,PAPER);
    lv_obj_center(l);
    lv_obj_add_event_cb(b,action,LV_EVENT_CLICKED,NULL);
    return b;
}

static void intro_opacity(void *object, int32_t opacity)
{
    lv_obj_set_style_opa(object, opacity, 0);
}

static void intro_show_page(void)
{
    lv_label_set_text(intro_title, intro_pages[intro_step].title);
    lv_label_set_text(intro_body, intro_pages[intro_step].body);
    bool last = intro_step + 1 == sizeof(intro_pages) / sizeof(intro_pages[0]);
    lv_label_set_text(intro_next, last ? "Ver mis fotos" : "Siguiente");
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, intro_body);
    lv_anim_set_exec_cb(&fade, intro_opacity);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade, 280);
    lv_anim_start(&fade);
}

static void intro_advance(lv_event_t *event)
{
    (void)event;
    if (intro_step + 1 == sizeof(intro_pages) / sizeof(intro_pages[0])) {
        if (welcome_schedule(false) != ESP_OK) {
            lv_anim_delete(intro_body, intro_opacity);
            lv_obj_set_style_opa(intro_body, LV_OPA_COVER, 0);
            lv_label_set_text(intro_body, "No se pudo guardar el final de la bienvenida. Vuelve a pulsar «Ver mis fotos».");
            return;
        }
        lv_obj_delete(intro_overlay);
        intro_overlay = NULL;
    } else {
        ++intro_step;
        intro_show_page();
    }
}

static void intro_start(void)
{
    intro_overlay = box(lv_layer_top(),0,0,480,320,COAL);
    lv_obj_remove_flag(intro_overlay, LV_OBJ_FLAG_SCROLL_CHAIN);
    box(intro_overlay,34,42,44,2,AMBER);
    intro_title = label(intro_overlay,34,63,"",AMBER);
    lv_obj_set_style_text_font(intro_title, &spanish_title_font, 0);
    lv_obj_set_width(intro_title, 412);
    intro_body = label(intro_overlay,34,106,"",PAPER);
    lv_obj_set_width(intro_body,408);
    lv_obj_set_style_text_line_space(intro_body, 7, 0);
    lv_obj_t *next = button(intro_overlay,310,252,136,"Siguiente",intro_advance);
    lv_obj_set_height(next,44);
    intro_next = lv_obj_get_child(next, 0);
    intro_show_page();
}

static void activate_hotspot(lv_event_t *event)
{
    (void)event;
    network_setup();
}

static void update_panel_help(lv_timer_t *timer)
{
    (void)timer;
    char ip[16], ssid[33], url[24] = "", help[232];
    panel_connection_t mode = network_panel_connection(ip, ssid);
    if (mode == PANEL_OFFLINE) {
        snprintf(help, sizeof(help), "La máquina está sin red. Activa %s para conectarte desde el móvil.", AP_NAME);
        lv_obj_remove_flag(panel_hotspot, LV_OBJ_FLAG_HIDDEN);
    } else {
        snprintf(url, sizeof(url), "http://%s/", ip);
        if (mode == PANEL_LAN && !*ssid)
            snprintf(help, sizeof(help), "Conecta el móvil a la misma red Wi-Fi que la máquina y escanea el código.");
        else
            snprintf(help, sizeof(help), "Conecta el móvil %s «%s». Después escanea el código o abre esta dirección.",
                mode == PANEL_LAN ? "a la misma red Wi-Fi" : "a la red Wi-Fi", ssid);
        lv_obj_add_flag(panel_hotspot, LV_OBJ_FLAG_HIDDEN);
    }
    if (strcmp(help, lv_label_get_text(panel_help))) lv_label_set_text(panel_help, help);
    if (strcmp(url, lv_label_get_text(panel_url))) {
        lv_label_set_text(panel_url, url);
        if (*url && lv_qrcode_update(panel_qr, url, strlen(url)) == LV_RESULT_OK)
            lv_obj_remove_flag(panel_qr, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panel_qr, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_panel_help(lv_event_t *event)
{
    (void)event;
    if (sheet) return;
    sheet = box(home,0,0,480,320,COAL);
    lv_obj_t *title = label(sheet,24,21,"Conectar al panel",AMBER);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    panel_help = label(sheet,24,76,"",PAPER);
    lv_obj_set_width(panel_help,230);
    panel_url = label(sheet,24,194,"",AMBER);
    panel_hotspot = button(sheet,24,170,176,"Activar " AP_NAME,activate_hotspot);
    panel_qr = lv_qrcode_create(sheet);
    lv_obj_set_pos(panel_qr,272,68);
    lv_qrcode_set_size(panel_qr,184);
    lv_qrcode_set_quiet_zone(panel_qr,true);
    lv_obj_add_flag(panel_qr, LV_OBJ_FLAG_HIDDEN);
    button(sheet,24,268,104,"Volver",close_note);
    update_panel_help(NULL);
    panel_timer = lv_timer_create(update_panel_help,1000,NULL);
}

static void create_keyboard(int y, int height)
{
    keyboard = lv_keyboard_create(sheet);
    lv_obj_set_size(keyboard,480,height);
    lv_obj_align(keyboard,LV_ALIGN_TOP_LEFT,0,y);
    static lv_font_t keys_font;
    keys_font = font_es_14;
    keys_font.fallback = &lv_font_montserrat_14;
    lv_obj_set_style_text_font(keyboard,&keys_font,LV_PART_ITEMS);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER,lower_keys,letter_widths);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_UPPER,upper_keys,letter_widths);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_SPECIAL,symbol_keys,symbol_widths);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_USER_1,date_keys,date_widths);
}

static void select_photo_field(lv_obj_t *field)
{
    lv_obj_clear_state(date_field, LV_STATE_FOCUSED);
    lv_obj_clear_state(place_field, LV_STATE_FOCUSED);
    lv_obj_add_state(field, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(keyboard,field);
    lv_keyboard_set_mode(keyboard,field == date_field ?
        LV_KEYBOARD_MODE_USER_1 : LV_KEYBOARD_MODE_TEXT_LOWER);
}

static void focus_photo_field(lv_event_t *event)
{
    select_photo_field(lv_event_get_target(event));
}

static void save_details(lv_event_t *event)
{
    (void)event;
    const char *date = lv_textarea_get_text(date_field);
    const char *place = lv_textarea_get_text(place_field);
    char iso[11] = "";
    if (*date) {
        if (strlen(date) == 10 && date[2] == '/' && date[5] == '/')
            snprintf(iso,sizeof(iso),"%.4s-%.2s-%.2s",date+6,date+3,date);
        if (!date_parse(iso)) {
            lv_label_set_text(note_status,"Fecha inválida. Usa DD/MM/AAAA.");
            return;
        }
    }
    if (strlen(place) > 160) {
        lv_label_set_text(note_status,"El nombre del lugar es demasiado largo.");
        return;
    }
    cJSON *meta = library_read_original(note_photo.id);
    const cJSON *rev = cJSON_GetObjectItemCaseSensitive(meta,"rev");
    lib_result_t result = LIB_CONFLICT;
    if (rev && rev->valuedouble == note_photo.rev) {
        cJSON_DeleteItemFromObjectCaseSensitive(meta,"date");
        cJSON_DeleteItemFromObjectCaseSensitive(meta,"place");
        result = cJSON_AddStringToObject(meta,"date",iso) && cJSON_AddStringToObject(meta,"place",place)
            ? library_edit(note_photo.id,note_photo.rev,meta) : LIB_IO;
    }
    cJSON_Delete(meta);
    if (result == LIB_OK) close_note(NULL);
    else if (result == LIB_FUTURE_DATE)
        lv_label_set_text(note_status,"La fecha no puede ser posterior a hoy.");
    else lv_label_set_text(note_status,result == LIB_CONFLICT ?
        "La ficha cambió. Ciérrala y vuelve a abrirla." : "No se pudo guardar la ficha.");
}

static lv_obj_t *photo_field(int x, int width, const char *title, const char *hint, const char *value)
{
    label(sheet,x,42,title,PAPER);
    lv_obj_t *field = lv_textarea_create(sheet);
    lv_obj_set_pos(field,x,62);
    lv_obj_set_size(field,width,40);
    lv_obj_set_style_text_font(field,&font_es_14,0);
    lv_textarea_set_one_line(field,true);
    lv_textarea_set_placeholder_text(field,hint);
    lv_textarea_set_text(field,value ? value : "");
    lv_obj_add_event_cb(field,focus_photo_field,LV_EVENT_CLICKED,NULL);
    return field;
}

static void edit_details(lv_event_t *event)
{
    if (!selected.id || (sheet && !scroll)) return;
    if (event && !sheet && (showing < 0 || frames[showing].id != selected.id)) return;
    bool edit_place = event && lv_event_get_target(event) == photo_location;
    cJSON *meta = library_read_original(selected.id);
    if (!meta) return;
    if (pending) { pending = false; request_photo(); }
    close_note(NULL);
    follow_timeline_start = false;
    note_photo = selected;
    note_photo.rev = (uint32_t)cJSON_GetObjectItemCaseSensitive(meta,"rev")->valuedouble;
    const char *place = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta,"place"));
    int date = date_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta,"date")));
    char date_text[16] = "";
    if (date) snprintf(date_text,sizeof(date_text),"%02d/%02d/%04d",date%100,date/100%100,date/10000);
    sheet = box(home,0,0,480,320,COAL);
    lv_obj_t *title = label(sheet,12,10,"Fecha y ubicación",AMBER);
    lv_obj_set_style_text_font(title,&spanish_title_font,0);
    date_field = photo_field(12,132,"Fecha","DD/MM/AAAA",date_text);
    lv_textarea_set_accepted_chars(date_field,"0123456789/");
    lv_textarea_set_max_length(date_field,10);
    bool gps = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(meta,"lat")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(meta,"lon"));
    place_field = photo_field(156,312,"Lugar",gps ? "Automático por GPS" : "Nombre del lugar",place);
    lv_textarea_set_max_length(place_field,160);
    cJSON_Delete(meta);
    create_keyboard(107,154);
    select_photo_field(edit_place ? place_field : date_field);
    button(sheet,12,280,85,"Guardar",save_details);
    button(sheet,104,280,90,"Cancelar",close_note);
    note_status = label(sheet,202,274,"",AMBER);
    lv_obj_set_width(note_status,258);
}

static void edit_note(lv_event_t *event)
{
    (void)event;
    cJSON *meta = library_read(note_photo.id);
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(meta,"rev");
    if (!rev || rev->valuedouble != note_photo.rev) {
        lv_label_set_text(note_status,"La nota cambió. Ciérrala y vuelve a abrirla.");
        cJSON_Delete(meta);
        return;
    }
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta, "note"));
    lv_obj_clean(sheet);
    scroll = NULL;
    editor = lv_textarea_create(sheet);
    lv_obj_set_pos(editor,12,23);
    lv_obj_set_size(editor,456,78);
    lv_obj_set_style_text_font(editor,&font_es_14,0);
    lv_textarea_set_max_length(editor,NOTE_LIMIT);
    lv_textarea_set_text(editor,value ? value : "");
    cJSON_Delete(meta);
    create_keyboard(107,154);
    lv_keyboard_set_textarea(keyboard,editor);
    button(sheet,12,280,85,"Guardar",save_note);
    button(sheet,104,280,90,"Cancelar",close_note);
    note_status = label(sheet,202,274,"",AMBER);
    lv_obj_set_width(note_status,258);
    lv_label_set_long_mode(note_status,LV_LABEL_LONG_WRAP);
}

static void open_note(lv_event_t *event)
{
    if (sheet || !selected.id) return;
    if (event && (showing < 0 || frames[showing].id != selected.id)) return;
    if (pending) { pending = false; request_photo(); }
    cJSON *meta = library_read(selected.id);
    if (!meta) return;
    follow_timeline_start = false;
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(meta, "rev");
    note_photo = selected;
    note_photo.rev = (uint32_t)rev->valuedouble;
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta,"note"));
    char place[LOCATION_TEXT_BYTES];
    location_text(meta, place);
    sheet = box(home,0,0,480,320,COAL);
    lv_obj_t *title = label(sheet,16,12,"Nota",AMBER);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    int scroll_top = 42;
    if (*place) {
        lv_obj_t *location = label(sheet,16,39,place,BRASS);
        lv_obj_set_width(location,448);
        lv_label_set_long_mode(location,LV_LABEL_LONG_DOT);
        scroll_top = 64;
    }
    scroll = box(sheet,16,scroll_top,448,264-scroll_top,COAL);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(BRASS), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(scroll, 3, LV_PART_SCROLLBAR);
    lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
    lv_obj_t *body = label(scroll,0,0,value && *value ? value : "Esta foto aún no tiene nota.",PAPER);
    lv_obj_remove_flag(body,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(body,430);
    lv_label_set_long_mode(body,LV_LABEL_LONG_WRAP);
    button(sheet,16,277,98,value && *value ? "Editar" : "Escribir",edit_note);
    button(sheet,122,277,252,"Editar fecha y ubicación",edit_details);
    button(sheet,386,277,78,"Cerrar",close_note);
    note_status = label(sheet,81,15,"",AMBER);
    lv_obj_set_width(note_status,370);
    lv_label_set_long_mode(note_status,LV_LABEL_LONG_WRAP);
    cJSON_Delete(meta);
}

static void inputs(void *arg)
{
    (void)arg;
    bool raw = false, stable = false, long_press = false;
    int64_t changed = 0, pressed = 0, last_status = 0;
    bool tap_pending = false, hold_pending = false;
    int detents = 0, photo_delta = 0;
    encoder_speed_t speed = {0};
    bool accelerated = false;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1));
        int step = board_encoder();
        int64_t now = esp_timer_get_time()/1000;
        if (step) {
            detents += step;
            int advance = encoder_photo_step(&speed, step, now);
            photo_delta += step * advance;
            accelerated = advance > 1;
        }
        bool button_down = board_pressed();
        if (raw != button_down) { raw = button_down; changed = now; }
        if (stable != raw && now-changed >= 24) {
            stable = raw;
            if (stable) { pressed = now; long_press = false; }
            else if (!long_press) tap_pending = true;
        }
        if (stable && !long_press && now-pressed >= 1600) {
            long_press = true;
            hold_pending = true;
        }
        if (!detents && !photo_delta && !tap_pending && !hold_pending && now-last_status < 20) continue;
        // Keep sampling quadrature while LVGL is drawing a frame.
        if (!lvgl_port_lock(1)) continue;
        bool loading = library_loading();
        uint32_t generation = library_generation();
        if (generation != last_generation) {
            last_generation = generation;
            clock_drawn = false;
            size_t next = follow_timeline_start ? 0 : library_position(selected.id,position);
            choose(next,false);
            if (sheet && note_photo.id && note_photo.id != selected.id) close_note(NULL);
        }
        if (loading != was_loading) {
            was_loading = loading;
            if (!loading) follow_timeline_start = false;
            if (!selected.id) render_photo();
        }
        if (intro_overlay) {
            speed = (encoder_speed_t){0};
        } else if (sheet) {
            if (scroll && detents) lv_obj_scroll_by(scroll,0,-detents*26,LV_ANIM_OFF);
            speed = (encoder_speed_t){0};
        } else if (detents || photo_delta) {
            size_t count = library_count();
            int movement = count > 12 ? photo_delta : detents;
            if (count && movement) {
                direction = movement > 0 ? 1 : -1;
                last_turn = speed.last_step_ms;
                int target = LV_CLAMP(0,(int)position + movement,(int)count-1);
                if ((size_t)target != position) {
                    follow_timeline_start = false;
                    choose(target, count > 12 && accelerated);
                }
            }
        }
        detents = photo_delta = 0;
        if (pending && now-last_turn >= 110) { pending = false; request_photo(); }
        if (hold_pending) {
            hold_pending = false;
            if (!intro_overlay && !editor) {
                if (selected.id && (!sheet || scroll)) edit_details(NULL);
                else if (!sheet) open_panel_help(NULL);
            }
        }
        if (tap_pending) {
            tap_pending = false;
            if (intro_overlay) intro_advance(NULL);
            else if (sheet && !editor) close_note(NULL);
            else if (!sheet) open_note(NULL);
        }
        if (now-last_status >= 20) {
            last_status = now;
            if (last_today != date_today()) update_labels();
        }
        lvgl_port_unlock();
    }
}

void gallery_start(bool intro)
{
    lvgl_port_lock(0);
    spanish_title_font = lv_font_montserrat_20;
    spanish_title_font.fallback = &font_es_14;
    lv_timer_set_period(lv_anim_get_timer(), 20);
    lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 20);
    home = lv_screen_active();
    lv_obj_remove_flag(home,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(home,lv_color_hex(COAL),0);
    lv_obj_t *photo_stage = box(home,0,0,480,PHOTO_STAGE_HEIGHT,COAL);
    lv_obj_add_event_cb(photo_stage,open_note,LV_EVENT_SHORT_CLICKED,NULL);
    picture = lv_image_create(photo_stage);
    lv_image_set_antialias(picture, false);
    paper_edge = box(photo_stage,0,0,1,2,0xd9ba80);
    lv_obj_add_flag(paper_edge, LV_OBJ_FLAG_HIDDEN);
    photo_location = label(photo_stage,0,0,"",PAPER);
    lv_obj_set_style_max_width(photo_location, 452, 0);
    lv_obj_set_style_bg_color(photo_location, lv_color_hex(COAL), 0);
    lv_obj_set_style_bg_opa(photo_location, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(photo_location, 8, 0);
    lv_obj_set_style_pad_ver(photo_location, 4, 0);
    lv_label_set_long_mode(photo_location, LV_LABEL_LONG_DOT);
    lv_obj_align(photo_location, LV_ALIGN_TOP_LEFT, 15, 6);
    lv_obj_add_flag(photo_location, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(photo_location,edit_details,LV_EVENT_SHORT_CLICKED,NULL);
    photo_note = label(photo_stage,0,0,"",PAPER);
    lv_obj_set_style_max_width(photo_note, 450, 0);
    lv_obj_set_height(photo_note, lv_font_get_line_height(&font_es_14) + 8);
    lv_obj_set_style_bg_color(photo_note, lv_color_hex(COAL), 0);
    lv_obj_set_style_bg_opa(photo_note, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(photo_note, 8, 0);
    lv_obj_set_style_pad_ver(photo_note, 4, 0);
    lv_label_set_long_mode(photo_note, LV_LABEL_LONG_DOT);
    lv_obj_align(photo_note, LV_ALIGN_BOTTOM_LEFT, 15, -6);
    lv_obj_add_flag(photo_note, LV_OBJ_FLAG_HIDDEN);
    welcome = box(home,0,0,480,PHOTO_STAGE_HEIGHT,COAL);
    avatar_frame = box(welcome,202,22,76,76,BRASS);
    lv_obj_set_style_radius(avatar_frame, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *crop = box(avatar_frame,2,2,72,72,COAL);
    lv_obj_set_style_radius(crop, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(crop, true, 0);
    box(crop,35,16,2,20,BRASS);
    box(crop,35,35,15,2,AMBER);
    avatar_image = lv_image_create(crop);
#ifdef HAS_PROFILE_AVATAR
    lv_image_set_src(avatar_image, &profile_avatar);
    lv_obj_center(avatar_image);
#endif
    lv_obj_t *title = label(welcome,0,116,DEVICE_TITLE,PAPER);
    lv_obj_set_width(title,460);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,116);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    lv_obj_t *rule = box(welcome,216,150,48,1,BRASS);
    lv_obj_remove_flag(rule,LV_OBJ_FLAG_CLICKABLE);
    placeholder = label(welcome,0,0,"",PAPER);
    lv_obj_set_style_text_align(placeholder,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(placeholder,460);
    lv_obj_align(placeholder,LV_ALIGN_TOP_MID,0,164);
    lv_obj_t *panel_button = button(welcome,168,200,144,"Panel web",open_panel_help);
    lv_obj_set_height(panel_button,42);
    box(home,0,PHOTO_STAGE_HEIGHT,480,1,0x423720);
    date_button = button(home,16,FOOTER_BUTTON_Y,132,"",edit_details);
    lv_obj_set_height(date_button,FOOTER_BUTTON_HEIGHT);
    lv_obj_set_style_pad_all(date_button,0,0);
    caption = lv_obj_get_child(date_button,0);
    age_label = label(home,0,0,"",AMBER);
    lv_obj_set_width(age_label,176);
    lv_obj_set_style_text_align(age_label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(age_label,LV_ALIGN_TOP_MID,0,
        FOOTER_BUTTON_Y + (FOOTER_BUTTON_HEIGHT - lv_font_get_line_height(&font_es_14)) / 2);
    lv_label_set_long_mode(age_label,LV_LABEL_LONG_DOT);
    lv_obj_t *connect = button(home,357,FOOTER_BUTTON_Y,106,"Conectar",open_panel_help);
    lv_obj_set_height(connect,FOOTER_BUTTON_HEIGHT);
    lv_obj_set_style_pad_all(connect,0,0);
    track = box(home,24,280,435,40,COAL);
    lv_obj_add_flag(track,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(track,jump,LV_EVENT_CLICKED,NULL);
    start_label = label(home,23,303,"",BRASS);
    end_label = label(home,422,303,"",BRASS);
    for (int i=0; i<CACHE_SIZE; ++i) {
        frames[i].image.header.magic = LV_IMAGE_HEADER_MAGIC;
        frames[i].image.header.cf = LV_COLOR_FORMAT_RGB565;
        frames[i].image.data = heap_caps_malloc(PIXELS*2,MALLOC_CAP_SPIRAM);
    }
    loads = xQueueCreate(1,sizeof(load_t));
    if (loads) xTaskCreate(loader,"photos",6144,NULL,3,NULL);
    choose(0,false);
    if (intro) intro_start();
    lvgl_port_unlock();
    xTaskCreate(inputs,"encoder",8192,NULL,5,NULL);
}

void gallery_profile_set(const uint8_t *pixels)
{
    if (!avatar_image) return;
    lvgl_port_lock(0);
    memcpy(custom_pixels,pixels,PROFILE_IMAGE_BYTES);
    lv_image_cache_drop(&custom_avatar);
    lv_image_set_src(avatar_image,&custom_avatar);
    lv_image_set_scale(avatar_image, 288);
    lv_obj_center(avatar_image);
    lv_obj_invalidate(avatar_image);
    lv_obj_remove_flag(avatar_frame,LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}
