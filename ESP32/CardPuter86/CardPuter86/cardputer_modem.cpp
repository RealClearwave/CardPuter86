#include "cardputer_modem.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <strings.h>

extern void write86(unsigned int addr32, unsigned char value);

static const uint16_t COM1_BASE = 0x3F8;
static const size_t RX_SIZE = 512;
static const size_t CMD_SIZE = 128;
static const size_t SSID_SIZE = 33;
static const size_t PASS_SIZE = 65;

static WiFiClient tcp_client;
static uint8_t rx_buf[RX_SIZE];
static volatile size_t rx_head = 0;
static volatile size_t rx_tail = 0;

static char cmd_buf[CMD_SIZE];
static size_t cmd_len = 0;
static char saved_ssid[SSID_SIZE];
static char saved_pass[PASS_SIZE];

static uint8_t ier = 0;
static uint8_t lcr = 0;
static uint8_t mcr = 0;
static uint8_t scr = 0;
static uint8_t dll = 1;
static uint8_t dlm = 0;
static bool echo_enabled = true;

static bool rx_available(void) {
    return rx_head != rx_tail;
}

static bool rx_push(uint8_t value) {
    const size_t next = (rx_head + 1) % RX_SIZE;
    if (next == rx_tail) return false;
    rx_buf[rx_head] = value;
    rx_head = next;
    return true;
}

static uint8_t rx_pop(void) {
    if (!rx_available()) return 0;
    const uint8_t value = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_SIZE;
    return value;
}

static void rx_write(const char *text) {
    while (*text) rx_push((uint8_t)*text++);
}

static void modem_response(const char *text) {
    rx_write("\r\n");
    rx_write(text);
    rx_write("\r\n");
}

static bool save_wifi_credentials(const char *ssid, const char *pass) {
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t ssid_result = nvs_set_str(handle, "wifi_ssid", ssid);
    const esp_err_t pass_result = nvs_set_str(handle, "wifi_pass", pass);
    const esp_err_t commit_result =
        ssid_result == ESP_OK && pass_result == ESP_OK ? nvs_commit(handle) : ESP_FAIL;
    nvs_close(handle);
    return ssid_result == ESP_OK && pass_result == ESP_OK && commit_result == ESP_OK;
}

static bool erase_wifi_credentials(void) {
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    nvs_erase_key(handle, "wifi_ssid");
    nvs_erase_key(handle, "wifi_pass");
    const esp_err_t commit_result = nvs_commit(handle);
    nvs_close(handle);
    return commit_result == ESP_OK;
}

static void load_wifi_credentials(void) {
    saved_ssid[0] = 0;
    saved_pass[0] = 0;
    if (nvs_flash_init() != ESP_OK) return;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return;
    size_t ssid_len = sizeof(saved_ssid);
    size_t pass_len = sizeof(saved_pass);
    nvs_get_str(handle, "wifi_ssid", saved_ssid, &ssid_len);
    nvs_get_str(handle, "wifi_pass", saved_pass, &pass_len);
    nvs_close(handle);
}

static bool parse_quoted_pair(const char *input, char *first, size_t first_len,
                              char *second, size_t second_len) {
    const char *p = strchr(input, '"');
    if (!p) return false;
    const char *q = strchr(++p, '"');
    if (!q) return false;
    const size_t a_len = q - p;
    if (a_len >= first_len) return false;
    memcpy(first, p, a_len);
    first[a_len] = 0;

    p = strchr(q + 1, '"');
    if (!p) return false;
    q = strchr(++p, '"');
    if (!q) return false;
    const size_t b_len = q - p;
    if (b_len >= second_len) return false;
    memcpy(second, p, b_len);
    second[b_len] = 0;
    return true;
}

static bool wifi_connect(const char *ssid, const char *pass, uint32_t timeout_ms) {
    if (!ssid || !ssid[0]) return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (uint32_t)(millis() - start) < timeout_ms) {
        delay(50);
    }
    return WiFi.status() == WL_CONNECTED;
}

static bool parse_host_port(const char *input, char *host, size_t host_len,
                            uint16_t *port) {
    while (*input == ' ' || *input == 'T' || *input == 't') input++;
    if (!*input) return false;

    const char *colon = strrchr(input, ':');
    if (!colon) return false;
    const long parsed_port = strtol(colon + 1, NULL, 10);
    if (parsed_port <= 0 || parsed_port > 65535) return false;

    size_t len = colon - input;
    while (len > 0 && input[len - 1] == ' ') len--;
    if (len == 0 || len >= host_len) return false;
    memcpy(host, input, len);
    host[len] = 0;
    *port = (uint16_t)parsed_port;
    return true;
}

static void modem_hangup(void) {
    if (tcp_client.connected()) tcp_client.stop();
    modem_response("NO CARRIER");
}

static void handle_command(const char *line) {
    if ((line[0] != 'A' && line[0] != 'a') ||
        (line[1] != 'T' && line[1] != 't')) {
        modem_response("ERROR");
        return;
    }

    const char *cmd = line + 2;
    if (*cmd == 0) {
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "Z")) {
        echo_enabled = true;
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "I")) {
        modem_response("CardPuter86 Wi-Fi Hayes Modem");
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "E0")) {
        echo_enabled = false;
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "E1")) {
        echo_enabled = true;
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "H") || !strcasecmp(cmd, "H0")) {
        modem_hangup();
        return;
    }
    if (!strncasecmp(cmd, "+CWJAP=", 7)) {
        char ssid[SSID_SIZE];
        char pass[PASS_SIZE];
        if (!parse_quoted_pair(cmd + 7, ssid, sizeof(ssid), pass, sizeof(pass))) {
            modem_response("ERROR");
            return;
        }
        modem_response("WIFI CONNECTING");
        if (!wifi_connect(ssid, pass, 15000)) {
            modem_response("NO CARRIER");
            return;
        }
        strncpy(saved_ssid, ssid, sizeof(saved_ssid));
        saved_ssid[sizeof(saved_ssid) - 1] = 0;
        strncpy(saved_pass, pass, sizeof(saved_pass));
        saved_pass[sizeof(saved_pass) - 1] = 0;
        save_wifi_credentials(saved_ssid, saved_pass);
        modem_response("WIFI CONNECTED");
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "+CWJAP?")) {
        if (saved_ssid[0]) {
            rx_write("\r\n+CWJAP:\"");
            rx_write(saved_ssid);
            rx_write("\"\r\n");
        } else {
            rx_write("\r\n+CWJAP:\"\"\r\n");
        }
        modem_response("OK");
        return;
    }
    if (!strcasecmp(cmd, "+WIFI?")) {
        modem_response(WiFi.status() == WL_CONNECTED ? "WIFI CONNECTED" : "WIFI DISCONNECTED");
        modem_response("OK");
        return;
    }
    if (cmd[0] == 'D' || cmd[0] == 'd') {
        char host[96];
        uint16_t port = 0;
        if (!parse_host_port(cmd + 1, host, sizeof(host), &port)) {
            modem_response("ERROR");
            return;
        }
        if (WiFi.status() != WL_CONNECTED &&
            !wifi_connect(saved_ssid, saved_pass, 10000)) {
            modem_response("NO DIALTONE");
            return;
        }
        if (tcp_client.connected()) tcp_client.stop();
        if (!tcp_client.connect(host, port)) {
            modem_response("BUSY");
            return;
        }
        modem_response("CONNECT 9600");
        return;
    }

    modem_response("ERROR");
}

static void command_byte(uint8_t value) {
    if (echo_enabled) rx_push(value);
    if (value == '\r' || value == '\n') {
        if (cmd_len == 0) return;
        cmd_buf[cmd_len] = 0;
        handle_command(cmd_buf);
        cmd_len = 0;
        return;
    }
    if (value == 8 || value == 127) {
        if (cmd_len > 0) cmd_len--;
        return;
    }
    if (cmd_len + 1 < sizeof(cmd_buf)) {
        cmd_buf[cmd_len++] = (char)value;
    }
}

void cardputer_modem_init(void) {
    if (tcp_client.connected()) tcp_client.stop();
    rx_head = rx_tail = 0;
    cmd_len = 0;
    ier = 0;
    lcr = 0;
    mcr = 0;
    scr = 0;
    dll = 1;
    dlm = 0;
    echo_enabled = true;
    load_wifi_credentials();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
}

const char *cardputer_modem_ssid(void) {
    return saved_ssid;
}

bool cardputer_modem_has_password(void) {
    return saved_pass[0] != 0;
}

bool cardputer_modem_set_wifi(const char *ssid, const char *password) {
    if (!ssid) ssid = "";
    if (!password) password = "";
    if (strlen(ssid) >= SSID_SIZE || strlen(password) >= PASS_SIZE) return false;
    strncpy(saved_ssid, ssid, sizeof(saved_ssid));
    saved_ssid[sizeof(saved_ssid) - 1] = 0;
    strncpy(saved_pass, password, sizeof(saved_pass));
    saved_pass[sizeof(saved_pass) - 1] = 0;
    return save_wifi_credentials(saved_ssid, saved_pass);
}

bool cardputer_modem_set_ssid(const char *ssid) {
    return cardputer_modem_set_wifi(ssid, saved_pass);
}

bool cardputer_modem_set_password(const char *password) {
    return cardputer_modem_set_wifi(saved_ssid, password);
}

bool cardputer_modem_clear_wifi(void) {
    saved_ssid[0] = 0;
    saved_pass[0] = 0;
    if (tcp_client.connected()) tcp_client.stop();
    WiFi.disconnect(true);
    return erase_wifi_credentials();
}

bool cardputer_modem_connect_saved(uint32_t timeout_ms) {
    return wifi_connect(saved_ssid, saved_pass, timeout_ms);
}

bool cardputer_modem_wifi_connected(void) {
    return WiFi.status() == WL_CONNECTED;
}

int cardputer_modem_scan_networks(CardputerWifiNetwork *networks, uint8_t max_networks) {
    if (!networks || max_networks == 0) return 0;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    const int found = WiFi.scanNetworks(false, true);
    if (found <= 0) return found;
    const int count = found < max_networks ? found : max_networks;
    for (int i = 0; i < count; i++) {
        String ssid = WiFi.SSID(i);
        strncpy(networks[i].ssid, ssid.c_str(), sizeof(networks[i].ssid));
        networks[i].ssid[sizeof(networks[i].ssid) - 1] = 0;
        networks[i].rssi = WiFi.RSSI(i);
        networks[i].encrypted = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    return count;
}

void cardputer_modem_install_bda(void) {
    write86(0x400, COM1_BASE & 0xFF);
    write86(0x401, COM1_BASE >> 8);
}

void cardputer_modem_poll(void) {
    if (tcp_client.connected()) {
        while (tcp_client.available() && ((rx_head + 1) % RX_SIZE) != rx_tail) {
            rx_push((uint8_t)tcp_client.read());
        }
    } else if (tcp_client) {
        tcp_client.stop();
    }
}

bool cardputer_modem_handles_port(uint16_t portnum) {
    return portnum >= COM1_BASE && portnum <= COM1_BASE + 7;
}

void cardputer_modem_port_out(uint16_t portnum, uint8_t value) {
    const uint8_t reg = portnum - COM1_BASE;
    const bool dlab = (lcr & 0x80) != 0;

    if (dlab && reg == 0) {
        dll = value;
        return;
    }
    if (dlab && reg == 1) {
        dlm = value;
        return;
    }

    switch (reg) {
        case 0:
            if (tcp_client.connected()) tcp_client.write(value);
            else command_byte(value);
            break;
        case 1:
            ier = value;
            break;
        case 3:
            lcr = value;
            break;
        case 4:
            mcr = value;
            break;
        case 7:
            scr = value;
            break;
        default:
            break;
    }
}

uint8_t cardputer_modem_port_in(uint16_t portnum) {
    const uint8_t reg = portnum - COM1_BASE;
    const bool dlab = (lcr & 0x80) != 0;

    if (dlab && reg == 0) return dll;
    if (dlab && reg == 1) return dlm;

    switch (reg) {
        case 0:
            return rx_pop();
        case 1:
            return ier;
        case 2:
            return rx_available() ? 0x04 : 0x01;
        case 3:
            return lcr;
        case 4:
            return mcr;
        case 5:
            return (rx_available() ? 0x01 : 0x00) | 0x20 | 0x40;
        case 6:
            return (tcp_client.connected() || WiFi.status() == WL_CONNECTED) ? 0xB0 : 0x30;
        case 7:
            return scr;
        default:
            return 0xFF;
    }
}
