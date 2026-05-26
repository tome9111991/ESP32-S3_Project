// SoftAP-Provisioning + HTTP-Server fuer Settings.
//
// Im Provisioning-Modus startet das Geraet einen SoftAP mit Random-Passwort,
// zeigt QR-Codes auf dem Display und nimmt unter http://192.168.4.1/
// Konfigurationen entgegen. Im Normal-Modus laeuft nur der HTTP-Server auf
// der STA-IP, sodass Settings spaeter ueber Browser nachbearbeitet werden.

#include "provisioning.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "qr_screen.h"
#include "settings.h"

static const char *TAG = "provisioning";

// Embedded HTML (kommt aus CMakeLists.txt via EMBED_TXTFILES).
extern const char web_ui_html_start[] asm("_binary_web_ui_html_start");
extern const char web_ui_html_end[]   asm("_binary_web_ui_html_end");

static httpd_handle_t s_server = nullptr;
static TaskHandle_t s_ssdp_task = nullptr;
static char s_ssdp_ip[16] = "";
static char s_ssdp_uuid[64] = "uuid:powerdashboard";

// =============================================================================
// HTTP-Handler
// =============================================================================

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, web_ui_html_start,
                    (ssize_t)(web_ui_html_end - web_ui_html_start));
    return ESP_OK;
}

static void get_request_base_url(httpd_req_t *req, char *out, size_t out_size)
{
    char host[64] = "powerdashboard.local";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        host[sizeof(host) - 1] = '\0';
    }
    snprintf(out, out_size, "http://%s/", host);
}

static esp_err_t device_xml_get(httpd_req_t *req)
{
    char base_url[96];
    char xml[1200];
    get_request_base_url(req, base_url, sizeof(base_url));

    // UPnP-Beschreibung fuer Fritzbox/SSDP mit anklickbarer Web-URL.
    int n = snprintf(xml, sizeof(xml),
        "<?xml version=\"1.0\"?>"
        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
          "<specVersion><major>1</major><minor>0</minor></specVersion>"
          "<URLBase>%s</URLBase>"
          "<device>"
            "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
            "<friendlyName>PowerDashboard</friendlyName>"
            "<manufacturer>Tommi</manufacturer>"
            "<modelName>PowerDashboard</modelName>"
            "<modelDescription>ESP32 PowerDashboard</modelDescription>"
            "<modelNumber>1</modelNumber>"
            "<serialNumber>%s</serialNumber>"
            "<UDN>%s</UDN>"
            "<presentationURL>%s</presentationURL>"
          "</device>"
        "</root>",
        base_url, s_ssdp_uuid, s_ssdp_uuid, base_url);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(xml)) n = sizeof(xml) - 1;

    httpd_resp_set_type(req, "text/xml; charset=utf-8");
    httpd_resp_send(req, xml, n);
    return ESP_OK;
}

// Captive-Portal: einige Smartphones fragen feste URLs ab, um zu pruefen
// ob ein Captive-Portal aktiv ist. Wir antworten immer mit unserer Seite.
static esp_err_t captive_get(httpd_req_t *req)
{
    return root_get(req);
}

static esp_err_t not_found_get(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    // Captive-Portal-Fallback: beliebige HTTP-Pfade zeigen die Setup-Seite.
    if (req->method == HTTP_GET) return root_get(req);
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
}

// GET /api/settings -> JSON-Array mit Meta + aktuellen Werten
static esp_err_t api_settings_get(httpd_req_t *req)
{
    size_t n = 0;
    const setting_meta_t *list = settings_meta_list(&n);

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < n; ++i) {
        const setting_meta_t *m = &list[i];
        cJSON *o = cJSON_CreateObject();
        if (!o) {
            cJSON_Delete(arr);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(o, "key",   m->nvs_key);
        cJSON_AddStringToObject(o, "label", m->label);
        cJSON_AddStringToObject(o, "group", m->group ? m->group : "");
        cJSON_AddBoolToObject  (o, "required", m->required);

        switch (m->type) {
        case SETTING_TYPE_STR: {
            cJSON_AddStringToObject(o, "type", "str");
            cJSON_AddNumberToObject(o, "maxlen", m->max_len);
            cJSON_AddBoolToObject  (o, "secret", m->secret);
            cJSON_AddStringToObject(o, "default", m->default_str ? m->default_str : "");
            char buf[128];
            settings_get_str(m->id, buf, sizeof(buf));
            if (m->secret) {
                cJSON_AddStringToObject(o, "value", "");
                cJSON_AddBoolToObject  (o, "has_value", buf[0] != '\0');
            } else {
                cJSON_AddStringToObject(o, "value", buf);
                cJSON_AddBoolToObject  (o, "has_value", buf[0] != '\0');
            }
            break;
        }
        case SETTING_TYPE_INT: {
            cJSON_AddStringToObject(o, "type", "int");
            cJSON_AddNumberToObject(o, "min", m->min_int);
            cJSON_AddNumberToObject(o, "max", m->max_int);
            cJSON_AddNumberToObject(o, "default", m->default_int);
            cJSON_AddNumberToObject(o, "value", settings_get_int(m->id));
            break;
        }
        case SETTING_TYPE_FLT: {
            cJSON_AddStringToObject(o, "type", "float");
            cJSON_AddNumberToObject(o, "min", m->min_flt);
            cJSON_AddNumberToObject(o, "max", m->max_flt);
            cJSON_AddNumberToObject(o, "default", m->default_flt);
            cJSON_AddNumberToObject(o, "value", settings_get_float(m->id));
            break;
        }
        }
        cJSON_AddItemToArray(arr, o);
    }
    char *txt = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!txt) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, txt, HTTPD_RESP_USE_STRLEN);
    free(txt);
    return ESP_OK;
}

// GET /api/diag -> kompakte Laufzeit-/Reset-Diagnose ohne serielle Konsole
static esp_err_t api_diag_get(httpd_req_t *req)
{
    char body[512];
    int n = powerdash_diag_json(body, sizeof(body));
    if (n <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "diag");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, n);
    return ESP_OK;
}

static esp_err_t read_json_body(httpd_req_t *req, cJSON **out)
{
    *out = nullptr;
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body size");
        return ESP_FAIL;
    }
    char *body = (char *)malloc(total + 1);
    if (!body) return ESP_ERR_NO_MEM;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) { free(body); return ESP_FAIL; }
        got += r;
    }
    body[total] = '\0';

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    *out = obj;
    return ESP_OK;
}

static int apply_settings_json(cJSON *obj)
{
    int applied = 0;
    cJSON *it = nullptr;
    cJSON_ArrayForEach(it, obj) {
        const setting_meta_t *m = settings_meta_by_key(it->string);
        if (!m) continue;
        if (m->type == SETTING_TYPE_STR && cJSON_IsString(it)) {
            settings_set_str(m->id, it->valuestring);
            applied++;
        } else if (m->type == SETTING_TYPE_INT) {
            int v = cJSON_IsNumber(it) ? (int)it->valuedouble
                  : cJSON_IsString(it) ? atoi(it->valuestring) : 0;
            settings_set_int(m->id, v);
            applied++;
        } else if (m->type == SETTING_TYPE_FLT) {
            float v = cJSON_IsNumber(it) ? (float)it->valuedouble
                    : cJSON_IsString(it) ? (float)atof(it->valuestring) : 0;
            settings_set_float(m->id, v);
            applied++;
        }
    }
    return applied;
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static bool schedule_reboot(void)
{
    return xTaskCreate(reboot_task, "reboot", 2048, nullptr, 4, nullptr) == pdPASS;
}

// POST /api/settings - Body: { "key": "value", ... }
static esp_err_t api_settings_post(httpd_req_t *req)
{
    cJSON *obj = nullptr;
    esp_err_t err = read_json_body(req, &obj);
    if (err != ESP_OK) return err;

    int applied = apply_settings_json(obj);
    cJSON_Delete(obj);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"applied\":%d}", applied);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    ESP_LOGI(TAG, "Settings POST: %d Werte geschrieben", applied);
    return ESP_OK;
}

// POST /api/settings/reboot speichert und startet danach in einem Request neu.
static esp_err_t api_settings_reboot_post(httpd_req_t *req)
{
    cJSON *obj = nullptr;
    esp_err_t err = read_json_body(req, &obj);
    if (err != ESP_OK) return err;

    int applied = apply_settings_json(obj);
    cJSON_Delete(obj);

    char resp[80];
    snprintf(resp, sizeof(resp), "{\"applied\":%d,\"reboot\":true}", applied);
    if (!schedule_reboot()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reboot task");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    ESP_LOGI(TAG, "Settings POST+Reboot: %d Werte geschrieben", applied);
    return ESP_OK;
}

static esp_err_t api_reboot_post(httpd_req_t *req)
{
    if (!schedule_reboot()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reboot task");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t api_factory_reset_post(httpd_req_t *req)
{
    settings_factory_reset();
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// =============================================================================
// Server-Start
// =============================================================================

static void register_handlers(httpd_handle_t s)
{
    httpd_uri_t u;
    memset(&u, 0, sizeof(u));

    u.method  = HTTP_GET;
    u.handler = root_get;
    u.uri     = "/";              httpd_register_uri_handler(s, &u);
    u.uri     = "/index.html";    httpd_register_uri_handler(s, &u);

    u.handler = device_xml_get;
    u.uri     = "/device.xml";    httpd_register_uri_handler(s, &u);
    u.uri     = "/description.xml"; httpd_register_uri_handler(s, &u);

    // Bekannte Captive-Portal-Pfade -> auf Setup-Seite umleiten
    u.handler = captive_get;
    u.uri = "/hotspot-detect.html";   httpd_register_uri_handler(s, &u);
    u.uri = "/generate_204";          httpd_register_uri_handler(s, &u);
    u.uri = "/connecttest.txt";       httpd_register_uri_handler(s, &u);
    u.uri = "/ncsi.txt";              httpd_register_uri_handler(s, &u);
    u.uri = "/redirect";              httpd_register_uri_handler(s, &u);

    u.handler = api_settings_get;
    u.uri = "/api/settings";          httpd_register_uri_handler(s, &u);

    u.handler = api_diag_get;
    u.uri = "/api/diag";              httpd_register_uri_handler(s, &u);

    u.method  = HTTP_POST;
    u.handler = api_settings_post;
    u.uri = "/api/settings";          httpd_register_uri_handler(s, &u);

    u.handler = api_settings_reboot_post;
    u.uri = "/api/settings/reboot";   httpd_register_uri_handler(s, &u);

    u.handler = api_reboot_post;
    u.uri = "/api/reboot";            httpd_register_uri_handler(s, &u);

    u.handler = api_factory_reset_post;
    u.uri = "/api/factory_reset";     httpd_register_uri_handler(s, &u);

    httpd_register_err_handler(s, HTTPD_404_NOT_FOUND, not_found_get);
}

static void start_http(void)
{
    if (s_server) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size       = 8192;
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) == ESP_OK) {
        register_handlers(s_server);
        ESP_LOGI(TAG, "HTTP-Server gestartet");
    } else {
        ESP_LOGE(TAG, "HTTP-Server start fehlgeschlagen");
        s_server = nullptr;
    }
}

extern "C" void provisioning_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
}

// =============================================================================
// SSDP/UPnP - liefert Fritzbox & Co. eine presentationURL zum Webinterface.
// =============================================================================

static bool contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return false;
    for (const char *h = haystack; *h; ++h) {
        const char *a = h;
        const char *b = needle;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b) return true;
    }
    return false;
}

static void ssdp_send_response(int sock, const struct sockaddr_in *dst,
                               const char *st)
{
    char usn[160];
    if (strcmp(st, s_ssdp_uuid) == 0) {
        snprintf(usn, sizeof(usn), "%s", s_ssdp_uuid);
    } else {
        snprintf(usn, sizeof(usn), "%s::%s", s_ssdp_uuid, st);
    }

    char msg[768];
    int n = snprintf(msg, sizeof(msg),
        "HTTP/1.1 200 OK\r\n"
        "CACHE-CONTROL: max-age=1800\r\n"
        "EXT:\r\n"
        "LOCATION: http://%s/device.xml\r\n"
        "SERVER: ESP-IDF UPnP/1.0 PowerDashboard/1.0\r\n"
        "ST: %s\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: 1\r\n"
        "CONFIGID.UPNP.ORG: 1\r\n"
        "\r\n",
        s_ssdp_ip, st, usn);
    if (n > 0 && n < (int)sizeof(msg)) {
        sendto(sock, msg, n, 0, (const struct sockaddr *)dst, sizeof(*dst));
    } else {
        ESP_LOGW(TAG, "SSDP response zu lang");
    }
}

static void ssdp_send_notify(int sock, const char *nt)
{
    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(1900);
    dst.sin_addr.s_addr = inet_addr("239.255.255.250");

    char usn[160];
    if (strcmp(nt, s_ssdp_uuid) == 0) {
        snprintf(usn, sizeof(usn), "%s", s_ssdp_uuid);
    } else {
        snprintf(usn, sizeof(usn), "%s::%s", s_ssdp_uuid, nt);
    }

    char msg[768];
    int n = snprintf(msg, sizeof(msg),
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "CACHE-CONTROL: max-age=1800\r\n"
        "LOCATION: http://%s/device.xml\r\n"
        "NT: %s\r\n"
        "NTS: ssdp:alive\r\n"
        "SERVER: ESP-IDF UPnP/1.0 PowerDashboard/1.0\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: 1\r\n"
        "CONFIGID.UPNP.ORG: 1\r\n"
        "\r\n",
        s_ssdp_ip, nt, usn);
    if (n > 0 && n < (int)sizeof(msg)) {
        sendto(sock, msg, n, 0, (const struct sockaddr *)&dst, sizeof(dst));
    } else {
        ESP_LOGW(TAG, "SSDP notify zu lang");
    }
}

static void ssdp_advertise(int sock)
{
    ssdp_send_notify(sock, "upnp:rootdevice");
    ssdp_send_notify(sock, s_ssdp_uuid);
    ssdp_send_notify(sock, "urn:schemas-upnp-org:device:Basic:1");
}

static void ssdp_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "SSDP-Socket fehlgeschlagen");
        s_ssdp_task = nullptr;
        vTaskDelete(nullptr);
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1900);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "SSDP-Bind fehlgeschlagen");
        close(sock);
        s_ssdp_task = nullptr;
        vTaskDelete(nullptr);
    }

    struct ip_mreq mreq = {};
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    uint8_t ttl = 2;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    ESP_LOGI(TAG, "SSDP aktiv: http://%s/device.xml", s_ssdp_ip);
    ssdp_advertise(sock);

    char buf[768];
    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv;
        tv.tv_sec = 60;
        tv.tv_usec = 0;
        int sel = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (sel == 0) {
            ssdp_advertise(sock);
            continue;
        }
        if (sel < 0 || !FD_ISSET(sock, &rfds)) continue;

        struct sockaddr_in src = {};
        socklen_t srclen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&src, &srclen);
        if (n <= 0) continue;
        buf[n] = '\0';

        // Fritzbox/Clients suchen per M-SEARCH nach ssdp:discover.
        if (!contains_ci(buf, "M-SEARCH") ||
            !contains_ci(buf, "ssdp:discover")) {
            continue;
        }

        if (contains_ci(buf, "ssdp:all") ||
            contains_ci(buf, "upnp:rootdevice")) {
            ssdp_send_response(sock, &src, "upnp:rootdevice");
        }
        if (contains_ci(buf, "ssdp:all") ||
            contains_ci(buf, s_ssdp_uuid)) {
            ssdp_send_response(sock, &src, s_ssdp_uuid);
        }
        if (contains_ci(buf, "ssdp:all") ||
            contains_ci(buf, "urn:schemas-upnp-org:device:Basic:1")) {
            ssdp_send_response(sock, &src,
                               "urn:schemas-upnp-org:device:Basic:1");
        }
    }
}

extern "C" void provisioning_start_ssdp(const char *ip_addr)
{
    if (s_ssdp_task || !ip_addr || ip_addr[0] == '\0') return;
    strncpy(s_ssdp_ip, ip_addr, sizeof(s_ssdp_ip) - 1);
    s_ssdp_ip[sizeof(s_ssdp_ip) - 1] = '\0';

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ssdp_uuid, sizeof(s_ssdp_uuid),
             "uuid:powerdashboard-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (xTaskCreate(ssdp_task, "ssdp", 4096, nullptr, 4, &s_ssdp_task) != pdPASS) {
        s_ssdp_task = nullptr;
        ESP_LOGE(TAG, "SSDP-Task konnte nicht starten");
    }
}

// =============================================================================
// Captive-DNS - antwortet auf jede A-Query mit 192.168.4.1, damit das
// Smartphone den Connectivity-Check als "Captive Portal" interpretiert und
// automatisch den Setup-Browser oeffnet.
// =============================================================================

static void dns_hijack_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS-Socket fehlgeschlagen");
        vTaskDelete(nullptr);
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS-Bind fehlgeschlagen");
        close(sock);
        vTaskDelete(nullptr);
    }
    ESP_LOGI(TAG, "DNS-Hijack auf Port 53 aktiv");

    const uint32_t ap_ip = htonl((192u << 24) | (168u << 16) | (4u << 8) | 1u);

    uint8_t buf[512];
    while (true) {
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &srclen);
        if (n < 12) continue;

        // QR-Bit setzen + Authoritative + RCODE=0
        buf[2] |= 0x80;
        buf[3] &= 0xF0;
        // ANCOUNT = 1
        buf[6] = 0; buf[7] = 1;
        // NSCOUNT / ARCOUNT = 0
        buf[8] = 0; buf[9] = 0; buf[10] = 0; buf[11] = 0;

        // Antwort hinten anhaengen: Pointer auf Name (0xC00C), TYPE=A,
        // CLASS=IN, TTL=60, RDLENGTH=4, RDATA=IP.
        int len = n;
        if (len + 16 > (int)sizeof(buf)) continue;  // zu lang
        buf[len++] = 0xC0; buf[len++] = 0x0C;
        buf[len++] = 0x00; buf[len++] = 0x01;       // TYPE A
        buf[len++] = 0x00; buf[len++] = 0x01;       // CLASS IN
        buf[len++] = 0x00; buf[len++] = 0x00;       // TTL hi
        buf[len++] = 0x00; buf[len++] = 0x3C;       // TTL = 60s
        buf[len++] = 0x00; buf[len++] = 0x04;       // RDLENGTH
        memcpy(buf + len, &ap_ip, 4);
        len += 4;

        sendto(sock, buf, len, 0, (struct sockaddr *)&src, srclen);
    }
}

static void start_dns_hijack(void)
{
    if (xTaskCreate(dns_hijack_task, "dns_hijack", 3072,
                    nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "DNS-Hijack-Task konnte nicht starten");
    }
}

static void configure_ap_captive_dhcp(esp_netif_t *ap_netif)
{
    if (!ap_netif) return;

    esp_netif_dhcp_status_t dhcps_status = ESP_NETIF_DHCP_INIT;
    esp_err_t err = esp_netif_dhcps_get_status(ap_netif, &dhcps_status);
    if (err == ESP_OK && dhcps_status == ESP_NETIF_DHCP_STARTED) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(ap_netif));
    }

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns));

    // DHCP-Option 6: Smartphone soll den ESP als DNS-Server benutzen.
    uint8_t offer_dns = 1;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_DOMAIN_NAME_SERVER,
                               &offer_dns, sizeof(offer_dns)));

    // DHCP-Option 114: moderne Clients bekommen die Portal-URL direkt.
    static const char captive_uri[] = "http://192.168.4.1/";
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_CAPTIVEPORTAL_URI,
                               (void *)captive_uri, strlen(captive_uri)));

    if (err == ESP_OK && dhcps_status == ESP_NETIF_DHCP_STARTED) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(ap_netif));
    }
}

// =============================================================================
// AP-Modus
// =============================================================================

static void make_ap_credentials(char *ssid, size_t ssid_size,
                                char *pass, size_t pass_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, ssid_size, "PowerDash-%02X%02X", mac[4], mac[5]);

    // 8-stelliges alphanumerisches Passwort (kein 0/O/1/l um Ablesefehler
    // zu vermeiden).
    static const char *alphabet =
        "ABCDEFGHJKMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
    size_t alen = strlen(alphabet);
    size_t len = 8;
    if (len >= pass_size) len = pass_size - 1;
    for (size_t i = 0; i < len; ++i) {
        pass[i] = alphabet[esp_random() % alen];
    }
    pass[len] = '\0';
}

extern "C" void provisioning_start_ap(char *ssid_out, size_t ssid_size,
                                      char *pass_out, size_t pass_size)
{
    char ssid[33], pass[33];
    make_ap_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    configure_ap_captive_dhcp(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap = {};
    size_t ssid_n = strnlen(ssid, sizeof(ssid));
    size_t pass_n = strnlen(pass, sizeof(pass));
    if (ssid_n > sizeof(ap.ap.ssid))     ssid_n = sizeof(ap.ap.ssid);
    if (pass_n > sizeof(ap.ap.password)) pass_n = sizeof(ap.ap.password);
    memcpy(ap.ap.ssid,     ssid, ssid_n);
    memcpy(ap.ap.password, pass, pass_n);
    ap.ap.ssid_len       = ssid_n;
    ap.ap.channel        = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_http();
    start_dns_hijack();

    if (ssid_out && ssid_size) {
        strncpy(ssid_out, ssid, ssid_size - 1);
        ssid_out[ssid_size - 1] = '\0';
    }
    if (pass_out && pass_size) {
        strncpy(pass_out, pass, pass_size - 1);
        pass_out[pass_size - 1] = '\0';
    }
    ESP_LOGI(TAG, "SoftAP up: SSID=%s PASS=%s", ssid, pass);
}

extern "C" void provisioning_start_http_only(void)
{
    start_http();
}
