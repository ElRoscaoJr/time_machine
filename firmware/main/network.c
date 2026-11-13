#include "network.h"
#include "calendar.h"
#include "config.h"
#include "library.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "gallery_profile.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_vfs_fat.h"

#define GOT_IP BIT0
#define LOST_IP BIT1
#define OPEN_AP BIT2
#define AP_IP "192.168.4.1"
#define STAGE LIB_ROOT "/upload.tmp"

typedef struct { char ssid[33], password[65]; } wifi_credentials_t;
typedef struct { wifi_credentials_t creds; int slot; } wifi_change_t;
static const char *const wifi_keys[] = {"wifi", "wifi2"};
enum { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED };
static atomic_int wifi_state;
static atomic_int wifi_reason;
static EventGroupHandle_t events;
static QueueHandle_t changes;
static esp_netif_t *ap_netif;
static _Atomic uint32_t lan_ip;
static atomic_bool access_point;
static nvs_handle_t settings;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

void network_address(char *buffer, size_t size)
{
    esp_ip4_addr_t ip = {.addr = atomic_load(&lan_ip)};
    if (ip.addr) snprintf(buffer,size,IPSTR,IP2STR(&ip));
    else if (atomic_load(&access_point)) snprintf(buffer,size,"%s",AP_IP);
    else snprintf(buffer,size,"%s","Sin red");
}

void network_setup(void)
{
    if (events) xEventGroupSetBits(events, OPEN_AP);
}

static bool read_exact(httpd_req_t *req, char *buffer, size_t length)
{
    while (length) {
        int got = httpd_req_recv(req,buffer,length);
        if (got <= 0) return false;
        length -= got;
        buffer += got;
    }
    return true;
}

static cJSON *read_json(httpd_req_t *req, size_t length)
{
    if (!length || length > JSON_LIMIT) return NULL;
    char *body = malloc(length+1);
    if (!body) return NULL;
    cJSON *json = NULL;
    if (read_exact(req,body,length) && !memchr(body,0,length)) {
        body[length] = 0;
        json = cJSON_ParseWithLengthOpts(body,length+1,NULL,true);
    }
    free(body);
    return json;
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req,status);
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,message);
}

static esp_err_t json_reply(httpd_req_t *req, cJSON *obj)
{
    char *body = obj ? cJSON_PrintUnformatted(obj) : NULL;
    cJSON_Delete(obj);
    if (!body) return reply(req,"500 Internal Server Error","Sin memoria");
    httpd_resp_set_type(req,"application/json");
    esp_err_t result = httpd_resp_sendstr(req,body);
    free(body);
    return result;
}

static esp_err_t lib_reply(httpd_req_t *req, lib_result_t result)
{
    switch (result) {
    case LIB_OK: return reply(req,"200 OK","Guardado");
    case LIB_INVALID: return reply(req,"400 Bad Request","Foto o datos no válidos");
    case LIB_MISSING: return reply(req,"404 Not Found","La foto ya no existe");
    case LIB_CONFLICT: return reply(req,"409 Conflict","La ficha cambió. Vuelve a abrirla antes de guardar");
    case LIB_FULL: return reply(req,"507 Insufficient Storage","Biblioteca llena");
    default: return reply(req,"503 Service Unavailable","No se pudo escribir en la microSD");
    }
}

static wifi_credentials_t load_wifi(int slot)
{
    wifi_credentials_t creds = {0};
    size_t size = sizeof(creds);
    if (nvs_get_blob(settings,wifi_keys[slot],&creds,&size) != ESP_OK || size != sizeof(creds))
        return (wifi_credentials_t){0};
    creds.ssid[32] = creds.password[64] = 0;
    return creds;
}

static esp_err_t profile(httpd_req_t *req)
{
    uint8_t *pixels = malloc(PROFILE_IMAGE_BYTES);
    if (!pixels) return reply(req,"500 Internal Server Error","Sin memoria");
    esp_err_t result;
    if (req->method == HTTP_GET) {
        size_t size = PROFILE_IMAGE_BYTES;
        if (nvs_get_blob(settings,"avatar",pixels,&size) == ESP_OK && size == PROFILE_IMAGE_BYTES) {
            httpd_resp_set_type(req,"application/octet-stream");
            result = httpd_resp_send(req,(const char *)pixels,size);
        } else result = reply(req,"404 Not Found","Sin avatar personalizado");
    } else if (req->content_len != PROFILE_IMAGE_BYTES || !read_exact(req,(char *)pixels,PROFILE_IMAGE_BYTES)) {
        result = reply(req,"400 Bad Request","Imagen de perfil no válida");
    } else if (nvs_set_blob(settings,"avatar",pixels,PROFILE_IMAGE_BYTES) != ESP_OK ||
               nvs_commit(settings) != ESP_OK) {
        result = reply(req,"500 Internal Server Error","No se pudo guardar el avatar");
    } else {
        gallery_profile_set(pixels);
        result = reply(req,"200 OK","Foto de perfil guardada");
    }
    free(pixels);
    return result;
}

static esp_err_t status(httpd_req_t *req)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return json_reply(req,NULL);
    char ip[32];
    network_address(ip,sizeof(ip));
    cJSON_AddStringToObject(obj,"ip",ip);
    cJSON_AddStringToObject(obj,"name",PROFILE_NAME);
    cJSON_AddNumberToObject(obj,"age",date_age(PROFILE_BIRTH,date_today()));
    cJSON_AddNumberToObject(obj,"count",library_count());
    cJSON_AddBoolToObject(obj,"sd",library_ready());
    cJSON_AddBoolToObject(obj,"ap",atomic_load(&access_point));
    cJSON_AddNumberToObject(obj,"wifiState",atomic_load(&wifi_state));
    cJSON_AddNumberToObject(obj,"wifiReason",atomic_load(&wifi_reason));
    cJSON_AddStringToObject(obj,"wifi1",load_wifi(0).ssid);
    cJSON_AddStringToObject(obj,"wifi2",load_wifi(1).ssid);
    uint64_t total, free_bytes;
    if (library_ready() && esp_vfs_fat_info(SD_ROOT, &total, &free_bytes) == ESP_OK)
        cJSON_AddNumberToObject(obj,"freeBytes",(double)free_bytes);
    return json_reply(req,obj);
}

static unsigned offset(httpd_req_t *req)
{
    char query[64], start[16];
    if (httpd_req_get_url_query_str(req,query,sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query,"offset",start,sizeof(start)) != ESP_OK) return 0;
    unsigned long parsed = strtoul(start,NULL,10);
    return parsed <= PHOTO_LIMIT ? parsed : PHOTO_LIMIT;
}

static esp_err_t list(httpd_req_t *req)
{
    unsigned start = offset(req);
    uint32_t generation = library_generation();
    cJSON *items = cJSON_CreateArray();
    if (!items) return json_reply(req,NULL);
    for (unsigned i = start; i < start+24; ++i) {
        photo_t photo;
        if (!library_at(i,&photo)) break;
        cJSON *obj = cJSON_CreateObject();
        if (!obj) { cJSON_Delete(items); return json_reply(req,NULL); }
        char id[17], date[11];
        snprintf(id,sizeof(id),"%016" PRIx64,photo.id);
        date_format(photo.date,date);
        cJSON_AddStringToObject(obj,"id",id);
        cJSON_AddStringToObject(obj,"date",date);
        cJSON_AddNumberToObject(obj,"rev",photo.rev);
        cJSON_AddItemToArray(items,obj);
    }
    cJSON *obj = cJSON_CreateObject();
    if (!obj) { cJSON_Delete(items); return json_reply(req,NULL); }
    cJSON_AddItemToObject(obj,"items",items);
    cJSON_AddNumberToObject(obj,"total",library_count());
    if (generation != library_generation()) {
        cJSON_Delete(obj);
        return reply(req,"409 Conflict","La biblioteca cambió. Vuelve a cargar la página");
    }
    return json_reply(req,obj);
}

static esp_err_t upload(httpd_req_t *req)
{
    char size[16], *end;
    if (!library_ready()) return lib_reply(req,LIB_IO);
    if (httpd_req_get_hdr_value_str(req,"X-Meta-Length",size,sizeof(size)) != ESP_OK)
        return lib_reply(req,LIB_INVALID);
    unsigned long meta_size = strtoul(size,&end,10);
    if (*end || !meta_size || meta_size > JSON_LIMIT || meta_size >= req->content_len ||
        req->content_len - meta_size > JPEG_LIMIT) return lib_reply(req,LIB_INVALID);
    cJSON *meta = read_json(req,meta_size);
    if (!meta) return lib_reply(req,LIB_INVALID);
    FILE *f = fopen(STAGE,"wb");
    lib_result_t result = LIB_IO;
    if (f) {
        char chunk[4096];
        size_t remaining = req->content_len - meta_size;
        while (remaining) {
            size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            if (!read_exact(req,chunk,n) || fwrite(chunk,1,n,f) != n) break;
            remaining -= n;
            taskYIELD();
        }
        bool ok = !remaining && !fflush(f) && !fsync(fileno(f));
        if (fclose(f)) ok = false;
        uint64_t id;
        if (ok) result = library_create(meta,STAGE,&id);
    }
    unlink(STAGE);
    cJSON_Delete(meta);
    return lib_reply(req,result);
}

static esp_err_t jpeg(httpd_req_t *req, uint64_t id)
{
    char path[64], chunk[2048];
    photo_path(id,"jpg",path);
    cJSON *meta = library_read(id);
    if (!meta) return lib_reply(req,LIB_MISSING);
    cJSON_Delete(meta);
    FILE *f = fopen(path,"rb");
    if (!f) return lib_reply(req,LIB_MISSING);
    httpd_resp_set_type(req,"image/jpeg");
    esp_err_t result = ESP_OK;
    size_t n;
    while ((n = fread(chunk,1,sizeof(chunk),f)) && result == ESP_OK)
        result = httpd_resp_send_chunk(req,chunk,n);
    fclose(f);
    return result == ESP_OK ? httpd_resp_send_chunk(req,NULL,0) : result;
}

static esp_err_t photo(httpd_req_t *req, const char *path)
{
    uint64_t id;
    char text[17];
    if (strlen(path) < 16) return lib_reply(req,LIB_MISSING);
    memcpy(text,path,16);
    text[16] = 0;
    if (!photo_id(text,&id)) return lib_reply(req,LIB_MISSING);
    if (req->method == HTTP_GET && !strcmp(path+16,"/image")) return jpeg(req,id);
    if (path[16]) return lib_reply(req,LIB_MISSING);
    if (req->method == HTTP_GET) {
        cJSON *obj = library_read(id);
        return obj ? json_reply(req,obj) : lib_reply(req,LIB_MISSING);
    }
    cJSON *doc = read_json(req,req->content_len);
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(doc,"rev");
    lib_result_t result = LIB_INVALID;
    if (cJSON_IsNumber(rev) && rev->valuedouble >= 1 && rev->valuedouble <= UINT32_MAX &&
        floor(rev->valuedouble) == rev->valuedouble) {
        if (req->method == HTTP_POST) result = library_edit(id,(uint32_t)rev->valuedouble,doc);
        else if (req->method == HTTP_DELETE) result = library_delete(id,(uint32_t)rev->valuedouble);
    }
    cJSON_Delete(doc);
    return lib_reply(req,result);
}

static esp_err_t wifi(httpd_req_t *req)
{
    cJSON *obj = read_json(req,req->content_len);
    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj,"ssid"));
    const char *password = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj,"password"));
    const cJSON *slot = cJSON_GetObjectItemCaseSensitive(obj,"slot");
    bool store_only = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj,"storeOnly"));
    wifi_change_t change = {0};
    bool valid = cJSON_IsNumber(slot) && (slot->valuedouble == 0 || slot->valuedouble == 1) && ssid && *ssid && strlen(ssid) <= 32 && password && strlen(password) <= 64 &&
        (!*password || strlen(password) >= 8) &&
        (strlen(password) != 64 || strspn(password,"0123456789abcdefABCDEF") == 64);
    if (valid) {
        change.slot = slot->valueint;
        strcpy(change.creds.ssid,ssid);
        strcpy(change.creds.password,password);
    }
    cJSON_Delete(obj);
    if (!valid) return reply(req,"400 Bad Request","Revisa el nombre y la contraseña de la red");
    if (store_only) {
        if (nvs_set_blob(settings,wifi_keys[change.slot],&change.creds,sizeof(change.creds)) != ESP_OK ||
            nvs_commit(settings) != ESP_OK)
            return reply(req,"500 Internal Server Error","No se pudo guardar la red");
        return reply(req,"200 OK","Red guardada para cuando esté disponible");
    }
    esp_err_t result = reply(req,"202 Accepted","Conectando. Consulta la IP en la pantalla");
    atomic_store(&wifi_reason,0);
    atomic_store(&wifi_state,WIFI_CONNECTING);
    xQueueOverwrite(changes,&change);
    return result;
}

static esp_err_t route(httpd_req_t *req)
{
    char host[64], ip[32], setup_host[32], lan_host[32];
    esp_ip4_addr_t addr = {.addr = atomic_load(&lan_ip)};
    snprintf(ip,sizeof(ip),IPSTR,IP2STR(&addr));
    snprintf(setup_host,sizeof(setup_host),AP_IP ":80");
    snprintf(lan_host,sizeof(lan_host),"%.15s:80",ip);
    bool setup = atomic_load(&access_point);
    bool host_read = httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host)) == ESP_OK;
    bool local_host = host_read && ((addr.addr && (!strcmp(host,ip) || !strcmp(host,lan_host))) ||
        (setup && (!strcmp(host,AP_IP) || !strcmp(host,setup_host))));
    if (!local_host) {
        if (setup && req->method == HTTP_GET) {
            httpd_resp_set_hdr(req,"Location","http://" AP_IP "/");
            return reply(req,"302 Found","Entretiempo");
        }
        return reply(req,"403 Forbidden","Abre la IP que aparece en pantalla");
    }
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    const char *uri = req->uri;
    if (req->method == HTTP_GET && !strncmp(uri,"/api/",5)) {
        if (!strcmp(uri,"/api/status")) return status(req);
        if (!strcmp(uri,"/api/profile")) return profile(req);
        if (!strcmp(uri,"/api/photos") || !strncmp(uri,"/api/photos?",12)) return list(req);
        if (!strncmp(uri,"/api/photos/",12)) return photo(req,uri+12);
        return reply(req,"404 Not Found","Ruta desconocida");
    }
    if (req->method == HTTP_GET) {
        if (strcmp(uri,"/")) {
            if (!setup) return reply(req,"404 Not Found","Ruta desconocida");
            httpd_resp_set_hdr(req,"Location","http://" AP_IP "/");
            return reply(req,"302 Found","Entretiempo");
        }
        httpd_resp_set_type(req,"text/html; charset=utf-8");
        httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
        return httpd_resp_send(req,index_html_start,index_html_end-index_html_start);
    }
    char mutation[4];
    if (httpd_req_get_hdr_value_str(req,"X-Panel",mutation,sizeof(mutation)) != ESP_OK || strcmp(mutation,"1"))
        return reply(req,"403 Forbidden","Solicitud no válida");
    if (req->method == HTTP_POST && !strcmp(uri,"/api/profile")) return profile(req);
    if (req->method == HTTP_POST && !strcmp(uri,"/api/photos")) return upload(req);
    if (req->method == HTTP_POST && !strcmp(uri,"/api/wifi")) return wifi(req);
    if (req->method == HTTP_POST && !strcmp(uri,"/api/time")) {
        cJSON *doc = read_json(req,req->content_len);
        cJSON *epoch = cJSON_GetObjectItemCaseSensitive(doc,"epoch");
        bool ok = cJSON_IsNumber(epoch) && epoch->valuedouble >= 1704067200.0 &&
            epoch->valuedouble < 4133980800.0 && date_sync((long long)epoch->valuedouble);
        cJSON_Delete(doc);
        return ok ? reply(req,"200 OK","Hora sincronizada") :
            reply(req,"400 Bad Request","Hora inválida");
    }
    if (!strncmp(uri,"/api/photos/",12)) return photo(req,uri+12);
    return reply(req,"404 Not Found","Ruta desconocida");
}

static void start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.stack_size = 12288;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    httpd_handle_t server;
    if (httpd_start(&server,&config) != ESP_OK) return;
    httpd_method_t methods[] = {HTTP_GET,HTTP_POST,HTTP_DELETE};
    for (unsigned i=0;i<sizeof(methods)/sizeof(*methods);++i) {
        httpd_uri_t handler = {.uri = "/*",.method = methods[i],.handler = route};
        httpd_register_uri_handler(server,&handler);
    }
}

static void dns(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    struct sockaddr_in listen = {.sin_family=AF_INET,.sin_port=htons(53),.sin_addr.s_addr=htonl(INADDR_ANY)};
    if (fd < 0 || bind(fd,(struct sockaddr *)&listen,sizeof(listen))) {
        if (fd >= 0) close(fd);
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        uint8_t msg[528];
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int n = recvfrom(fd,msg,512,0,(struct sockaddr *)&from,&len);
        if (!atomic_load(&access_point) || n < 17 || msg[4] || msg[5] != 1 || (msg[2] & 0xf8)) continue;
        if ((ntohl(from.sin_addr.s_addr) & 0xffffff00U) != 0xc0a80400U) continue;
        int i = 12;
        while (i < n && msg[i] && msg[i] <= 63) i += msg[i]+1;
        if (i+5 > n || msg[i]) continue;
        bool a = msg[i+1] == 0 && msg[i+2] == 1 && msg[i+3] == 0 && msg[i+4] == 1;
        i += 5;
        msg[2] = 0x81;
        msg[3] = 0x80;
        memset(msg+6,0,6);
        if (a) {
            const uint8_t record[] = {0xc0,12,0,1,0,1,0,0,0,0,0,4,192,168,4,1};
            msg[7] = 1;
            memcpy(msg+i,record,sizeof(record));
            i += sizeof(record);
        }
        sendto(fd,msg,i,0,(struct sockaddr *)&from,len);
    }
}

static void wifi_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        atomic_store(&lan_ip,event->ip_info.ip.addr);
        xEventGroupSetBits(events,GOT_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        esp_netif_dns_info_t dns = {.ip = {.type = ESP_IPADDR_TYPE_V4}};
        dns.ip.u_addr.ip4.addr = inet_addr(AP_IP);
        uint8_t offer_dns = 0x02;
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_dhcps_option(ap_netif,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,
                              &offer_dns,sizeof(offer_dns));
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
        esp_netif_dhcps_start(ap_netif);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        atomic_store(&lan_ip,0);
        if (event->reason != WIFI_REASON_ASSOC_LEAVE) {
            atomic_store(&wifi_reason,event->reason);
            ESP_LOGI("wifi-config","Desconexión Wi-Fi, motivo %u",(unsigned)event->reason);
        }
        xEventGroupSetBits(events,LOST_IP);
    }
}

static void set_ap(bool enable)
{
    if (enable && (strlen(AP_PASSWORD) < 8 || strlen(AP_PASSWORD) > 63)) return;
    if (atomic_load(&access_point) == enable) return;
    if (esp_wifi_set_mode(enable ? WIFI_MODE_APSTA : WIFI_MODE_STA) == ESP_OK)
        atomic_store(&access_point,enable);
}

static bool connect_wifi(const wifi_credentials_t *creds)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid,creds->ssid,strlen(creds->ssid));
    memcpy(config.sta.password,creds->password,strlen(creds->password));
    config.sta.threshold.authmode = *creds->password ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    return esp_wifi_set_config(WIFI_IF_STA,&config) == ESP_OK && esp_wifi_connect() == ESP_OK;
}

static void manage(void *arg)
{
    (void)arg;
    wifi_credentials_t creds = load_wifi(0);
    int slot = 0;
    bool manual = false, save = false, tried_second = false, trying = false;
    TickType_t deadline = 0, close_ap = 0;
    if (!creds.ssid[0]) {
        creds = load_wifi(1);
        slot = 1;
        tried_second = true;
    }
    if (creds.ssid[0]) {
        trying = true;
        atomic_store(&wifi_state,WIFI_CONNECTING);
        deadline = xTaskGetTickCount() + pdMS_TO_TICKS(18000);
        if (!connect_wifi(&creds)) deadline = xTaskGetTickCount();
    } else set_ap(true);
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(events,GOT_IP|LOST_IP|OPEN_AP,pdTRUE,pdFALSE,pdMS_TO_TICKS(200));
        TickType_t now = xTaskGetTickCount();
        if (bits & OPEN_AP) {
            manual = !manual;
            set_ap(manual || !atomic_load(&lan_ip));
        }
        wifi_change_t candidate;
        if (xQueueReceive(changes,&candidate,0) == pdTRUE) {
            set_ap(true);
            esp_wifi_disconnect();
            atomic_store(&lan_ip,0);
            atomic_store(&wifi_reason,0);
            atomic_store(&wifi_state,WIFI_CONNECTING);
            xEventGroupClearBits(events,GOT_IP|LOST_IP);
            creds = candidate.creds;
            slot = candidate.slot;
            trying = save = true;
            close_ap = 0;
            deadline = now + pdMS_TO_TICKS(18000);
            if (!connect_wifi(&creds)) deadline = now;
        } else if ((bits & GOT_IP) && atomic_load(&lan_ip)) {
            trying = false;
            esp_netif_sntp_start();
            if (save && (nvs_set_blob(settings,wifi_keys[slot],&creds,sizeof(creds)) != ESP_OK ||
                         nvs_commit(settings) != ESP_OK)) {
                ESP_LOGE("wifi-config","No se pudo guardar la red");
            }
            atomic_store(&wifi_reason,0);
            atomic_store(&wifi_state,WIFI_CONNECTED);
            close_ap = save && atomic_load(&access_point) ? now + pdMS_TO_TICKS(25000) : 0;
            save = manual = false;
            if (!close_ap) set_ap(false);
        } else if (trying && (int32_t)(now-deadline) >= 0) {
            esp_wifi_disconnect();
            if (!save && !tried_second) {
                tried_second = true;
                slot = 1-slot;
                creds = load_wifi(slot);
                if (creds.ssid[0]) {
                    deadline = now + pdMS_TO_TICKS(18000);
                    if (!connect_wifi(&creds)) deadline = now;
                    continue;
                }
            }
            trying = save = false;
            atomic_store(&wifi_state,WIFI_FAILED);
            set_ap(true);
        } else if (trying && (bits & LOST_IP) && !atomic_load(&lan_ip)) {
            esp_wifi_connect();
        } else if (!trying && (bits & LOST_IP) && atomic_load(&wifi_state) == WIFI_CONNECTED) {
            close_ap = 0;
            tried_second = false;
            trying = true;
            atomic_store(&wifi_state,WIFI_CONNECTING);
            deadline = now + pdMS_TO_TICKS(18000);
            if (!connect_wifi(&creds)) deadline = now;
        }
        if (close_ap && (int32_t)(now-close_ap) >= 0) {
            close_ap = 0;
            if (!manual && atomic_load(&lan_ip)) set_ap(false);
        }
    }
}

void network_start(void)
{
    if (nvs_open("entretiempo",NVS_READWRITE,&settings) != ESP_OK) return;
    uint8_t *pixels = malloc(PROFILE_IMAGE_BYTES);
    if (pixels) {
        size_t size = PROFILE_IMAGE_BYTES;
        if (nvs_get_blob(settings,"avatar",pixels,&size) == ESP_OK && size == PROFILE_IMAGE_BYTES)
            gallery_profile_set(pixels);
        free(pixels);
    }
    events = xEventGroupCreate();
    changes = xQueueCreate(1,sizeof(wifi_change_t));
    if (!events || !changes) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t ap = {.ap = {.ssid = AP_NAME,.channel=1,.max_connection=3,.authmode=WIFI_AUTH_WPA2_PSK}};
    bool provisioned = strlen(AP_PASSWORD) >= 8 && strlen(AP_PASSWORD) <= 63;
    if (provisioned) {
        memcpy(ap.ap.password,AP_PASSWORD,strlen(AP_PASSWORD));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap));
        atomic_store(&access_point,true);
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    atomic_store(&access_point,false);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_sntp_config_t time = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time.start = false;
    esp_netif_sntp_init(&time);
    start_http();
    xTaskCreate(dns,"portal-dns",4096,NULL,3,NULL);
    xTaskCreate(manage,"wifi-config",4096,NULL,3,NULL);
}
