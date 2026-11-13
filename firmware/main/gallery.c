#include "board.h"
#include "calendar.h"
#include "config.h"
#include "encoder_speed.h"
#include "library.h"
#include "gallery_profile.h"
#include "network.h"
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

#define DEVICE_TITLE "Daniela’s Time Machine"
#define PAPER 0xece1cb
#define BRASS 0xa68a56
#define AMBER 0xffbd62
#define COAL 0x141311
#define TRACK 424
#define TRACK_INSET (MARKER_SIZE / 2)
#define MARKER_SIZE 10
#define TURN_MS 144
#define CACHE_SIZE 3

typedef struct {
    lv_image_dsc_t image;
    uint64_t id;
} frame_t;

typedef struct {
    uint64_t id, next, other;
} load_t;

static frame_t frames[CACHE_SIZE];
static QueueHandle_t loads;
static int showing = -1;
static int turn_target = -1;
static int turn_direction;
static bool turning, turned;
static lv_obj_t *home, *picture, *caption, *age_label, *address, *track, *cursor;
static lv_obj_t *end_label, *start_label, *placeholder, *welcome, *paper_edge, *track_fill;
static lv_obj_t *sheet, *editor, *keyboard, *scroll, *note_status, *note_ip;
static photo_t selected, note_photo;
static size_t position;
static uint32_t last_generation;
static int first_day, last_day, last_today, direction = 1;
static int64_t last_turn;
static bool pending, clock_drawn;
static uint64_t failed;
static const char *keymap[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","ñ","\n",
    "z","x","c","v","b","n","m",",",".",LV_SYMBOL_BACKSPACE,"\n",
    "á","é","í","ó","ú","ü","¿","?"," ","Enter",""
};
static const lv_buttonmatrix_ctrl_t keyctrl[] = {
    1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,2,
    1,1,1,1,1,1,1,1,4,3,
};

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

static void show_frame(int slot)
{
    showing = slot;
    lv_image_set_src(picture, &frames[slot].image);
    lv_image_set_pivot(picture, frames[slot].image.header.w/2, frames[slot].image.header.h/2);
    lv_obj_center(picture);
    lv_obj_add_flag(welcome, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(picture, LV_OBJ_FLAG_HIDDEN);
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
    unsigned scale = 256 - distance * 240 / 128;
    lv_image_set_scale_y(picture, scale);
    int offset = -turn_direction * distance / 10;
    lv_obj_align(picture, LV_ALIGN_CENTER, 0, offset);
    lv_obj_align(paper_edge, LV_ALIGN_CENTER, 0, offset);
    if (distance >= 100) {
        lv_obj_set_width(paper_edge, frames[showing].image.header.w);
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
        lv_label_set_text_fmt(placeholder, "%s\n\n%s", DEVICE_TITLE,
            !library_ready() ? "Inserta una microSD FAT32 y reinicia" :
            selected.id ? "No se pudo abrir esta foto" : "Añade fotos desde el panel o la microSD");
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
    load_t load = {.id = selected.id};
    if ((direction > 0 || position > 0) && library_at(position + direction, &neighbor)) load.next = neighbor.id;
    if ((direction < 0 || position > 0) && library_at(position - direction, &neighbor)) load.other = neighbor.id;
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
            int slot = -1;
            if (cached(ids[n]) < 0 && failed != ids[n]) {
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
            char path[64];
            photo_path(ids[n], "jpg", path);
            unsigned w, h;
            bool ok = photo_decode(path, (void *)frames[slot].image.data, &w, &h);
            lvgl_port_lock(0);
            if (ok) {
                frame_t *frame = &frames[slot];
                frame->image.header.w = w;
                frame->image.header.h = h;
                frame->image.header.stride = w*2;
                frame->image.data_size = w*h*2;
                frame->id = ids[n];
            } else if (ids[n] == selected.id) failed = ids[n];
            if (!pending && ids[n] == selected.id) render_photo();
            lvgl_port_unlock();
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
    int end = today ? today : library_last_date();
    if (end < start) end = start;
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
    char ip[32];
    network_address(ip,sizeof(ip));
    if (strcmp(ip, lv_label_get_text(address))) {
        lv_label_set_text(address, ip);
        if (note_ip) lv_label_set_text(note_ip, ip);
    }
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
        lv_label_set_text(caption, selected.id ? "Sin fecha" : "");
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
    direction = target < position ? -1 : 1;
    choose(target, false);
}

static void close_note(lv_event_t *event)
{
    (void)event;
    if (!sheet) return;
    lv_obj_delete(sheet);
    sheet = editor = keyboard = scroll = note_status = note_ip = NULL;
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

static void open_panel_help(lv_event_t *event)
{
    (void)event;
    sheet = box(home,0,0,480,320,COAL);
    lv_obj_t *title = label(sheet,24,24,"Panel web",AMBER);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    lv_obj_t *help = label(sheet,24,69,
        "Abre en el móvil la dirección que aparece abajo.\n\n"
        "Si es 192.168.4.1, conecta antes el móvil a la red time_machine.\n\n"
        "Para guardar recuerdos, inserta una microSD FAT32 y reinicia.",PAPER);
    lv_obj_set_width(help,432);
    lv_label_set_long_mode(help,LV_LABEL_LONG_WRAP);
    note_ip = label(sheet,24,220,lv_label_get_text(address),BRASS);
    button(sheet,24,263,104,"Volver",close_note);
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
    note_ip = label(sheet,330,4,lv_label_get_text(address),BRASS);
    lv_obj_set_style_text_align(note_ip,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_set_width(note_ip,134);
    editor = lv_textarea_create(sheet);
    lv_obj_set_pos(editor,12,23);
    lv_obj_set_size(editor,456,78);
    lv_obj_set_style_text_font(editor,&font_es_14,0);
    lv_textarea_set_max_length(editor,NOTE_LIMIT);
    lv_textarea_set_text(editor,value ? value : "");
    cJSON_Delete(meta);
    keyboard = lv_keyboard_create(sheet);
    lv_obj_set_pos(keyboard,0,107);
    lv_obj_set_size(keyboard,480,154);
    static lv_font_t keys_font;
    keys_font = font_es_14;
    keys_font.fallback = &lv_font_montserrat_14;
    lv_obj_set_style_text_font(keyboard,&keys_font,LV_PART_ITEMS);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_USER_1,keymap,keyctrl);
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(keyboard,editor);
    button(sheet,12,280,85,"Guardar",save_note);
    button(sheet,104,280,90,"Cancelar",close_note);
    note_status = label(sheet,202,274,"",AMBER);
    lv_obj_set_width(note_status,258);
    lv_label_set_long_mode(note_status,LV_LABEL_LONG_WRAP);
}

static void open_note(void)
{
    if (!selected.id) return;
    note_photo = selected;
    cJSON *meta = library_read(selected.id);
    if (!meta) return;
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(meta, "rev");
    note_photo.rev = (uint32_t)rev->valuedouble;
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta,"note"));
    const char *place = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meta,"place"));
    sheet = box(home,0,0,480,320,COAL);
    lv_obj_t *title = label(sheet,16,12,"Nota",AMBER);
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    lv_obj_t *location = label(sheet,95,16,place ? place : "",BRASS);
    lv_obj_set_width(location,265);
    lv_label_set_long_mode(location,LV_LABEL_LONG_DOT);
    note_ip = label(sheet,350,16,lv_label_get_text(address),BRASS);
    lv_obj_set_width(note_ip,120);
    lv_obj_set_style_text_align(note_ip,LV_TEXT_ALIGN_RIGHT,0);
    scroll = box(sheet,16,42,448,222,COAL);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(BRASS), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(scroll, 3, LV_PART_SCROLLBAR);
    lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
    lv_obj_t *body = label(scroll,0,0,value && *value ? value : "Esta foto aún no tiene nota.",PAPER);
    lv_obj_set_width(body,430);
    lv_label_set_long_mode(body,LV_LABEL_LONG_WRAP);
    button(sheet,16,277,98,value && *value ? "Editar" : "Escribir",edit_note);
    button(sheet,122,277,80,"Cerrar",close_note);
    note_status = label(sheet,210,274,"",AMBER);
    lv_obj_set_width(note_status,250);
    lv_label_set_long_mode(note_status,LV_LABEL_LONG_WRAP);
    cJSON_Delete(meta);
}

static void inputs(void *arg)
{
    (void)arg;
    bool raw = false, stable = false, long_press = false;
    int64_t changed = 0, pressed = 0, last_status = 0;
    bool tap_pending = false;
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
            network_setup();
        }
        if (!detents && !photo_delta && !tap_pending && now-last_status < 20) continue;
        // Keep sampling quadrature while LVGL is drawing a frame.
        if (!lvgl_port_lock(1)) continue;
        uint32_t generation = library_generation();
        if (generation != last_generation) {
            last_generation = generation;
            clock_drawn = false;
            choose(library_position(selected.id,position),false);
        }
        if (sheet) {
            if (scroll && detents) lv_obj_scroll_by(scroll,0,-detents*26,LV_ANIM_OFF);
            speed = (encoder_speed_t){0};
        } else if (photo_delta && library_count()) {
            direction = photo_delta > 0 ? 1 : -1;
            last_turn = speed.last_step_ms;
            int target = (int)position + photo_delta;
            target = LV_CLAMP(0,target,(int)library_count()-1);
            if ((size_t)target != position) choose(target, accelerated);
        }
        detents = photo_delta = 0;
        if (pending && now-last_turn >= 110) { pending = false; request_photo(); }
        if (tap_pending) {
            tap_pending = false;
            if (sheet && !editor) close_note(NULL);
            else if (!sheet) {
                if (pending) { pending = false; request_photo(); }
                open_note();
            }
        }
        if (now-last_status >= 20) {
            last_status = now;
            char ip[32];
            network_address(ip,sizeof(ip));
            if (last_today != date_today() || strcmp(ip,lv_label_get_text(address))) update_labels();
        }
        lvgl_port_unlock();
    }
}

void gallery_start(void)
{
    lvgl_port_lock(0);
    lv_timer_set_period(lv_anim_get_timer(), 20);
    lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 20);
    home = lv_screen_active();
    lv_obj_remove_flag(home,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(home,lv_color_hex(COAL),0);
    lv_obj_t *photo_stage = box(home,0,0,480,254,COAL);
    picture = lv_image_create(photo_stage);
    lv_image_set_antialias(picture, false);
    paper_edge = box(photo_stage,0,0,1,2,0xd9ba80);
    lv_obj_add_flag(paper_edge, LV_OBJ_FLAG_HIDDEN);
    welcome = box(home,0,0,480,254,COAL);
    int text_y = 110;
    avatar_frame = box(welcome,202,39,76,76,BRASS);
    lv_obj_set_style_radius(avatar_frame, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *crop = box(avatar_frame,2,2,72,72,COAL);
    lv_obj_set_style_radius(crop, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(crop, true, 0);
    avatar_image = lv_image_create(crop);
#ifdef HAS_PROFILE_AVATAR
    lv_image_set_src(avatar_image, &profile_avatar);
    lv_obj_center(avatar_image);
    text_y = 130;
#else
    lv_obj_add_flag(avatar_frame,LV_OBJ_FLAG_HIDDEN);
#endif
    placeholder = label(welcome,0,text_y,DEVICE_TITLE,PAPER);
    lv_obj_set_style_text_align(placeholder,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(placeholder,460);
    lv_obj_align(placeholder,LV_ALIGN_TOP_MID,0,text_y);
    button(welcome,168,195,144,"Panel web",open_panel_help);
    box(home,0,254,480,1,0x423720);
    caption = label(home,23,258,"",PAPER);
    lv_obj_set_style_text_font(caption,&lv_font_montserrat_20,0);
    age_label = label(home,162,262,"",AMBER);
    address = label(home,341,262,"",BRASS);
    lv_obj_set_width(address,116);
    lv_obj_set_style_text_align(address,LV_TEXT_ALIGN_RIGHT,0);
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
    lv_obj_align(placeholder,LV_ALIGN_TOP_MID,0,130);
    lvgl_port_unlock();
}
