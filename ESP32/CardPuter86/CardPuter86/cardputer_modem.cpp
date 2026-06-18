#include "cardputer_modem.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <nvs.h>

// Minimal COM1 8250/16550 + Hayes modem. Keep this deliberately small:
// no PPP, no DNS cache, no terminal UI, no dynamic buffers.

#define COM1_BASE 0x3F8
#define UART_LSR_DR 0x01
#define UART_LSR_THRE 0x20
#define UART_LSR_TEMT 0x40
#define UART_MSR_CTS 0x10
#define UART_MSR_DSR 0x20
#define UART_MSR_RI  0x40
#define UART_MSR_DCD 0x80

static WiFiClient tcp_client;
static uint8_t ier = 0;
static uint8_t lcr = 0;
static uint8_t mcr = 0;
static uint8_t scr = 0;
static uint16_t divisor = 12; // 9600 bps compatible default

static bool echo_on = true;
static bool command_mode = true;
static bool wifi_started = false;
static uint32_t last_wifi_attempt_ms = 0;
static uint8_t plus_count = 0;

static char wifi_ssid[33] = {};
static char wifi_pass[65] = {};
static char cmd_line[96] = {};
static uint8_t cmd_len = 0;

static uint8_t rx_buf[256];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

static void start_wifi(void);

static bool rx_empty(void) {
    return rx_head == rx_tail;
}

static bool rx_full(void) {
    return (uint8_t)(rx_head + 1) == rx_tail;
}

static void rx_push(uint8_t value) {
    if (rx_full()) return;
    rx_buf[rx_head++] = value;
}

static int rx_pop(void) {
    if (rx_empty()) return -1;
    const uint8_t value = rx_buf[rx_tail++];
    return value;
}

static void queue_text(const char *text) {
    while (*text) rx_push((uint8_t)*text++);
}

static void result(const char *text) {
    queue_text("\r\n");
    queue_text(text);
    queue_text("\r\n");
}

static void load_wifi_settings(void) {
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return;
    size_t len = sizeof(wifi_ssid);
    nvs_get_str(handle, "wifi_ssid", wifi_ssid, &len);
    len = sizeof(wifi_pass);
    nvs_get_str(handle, "wifi_pass", wifi_pass, &len);
    nvs_close(handle);
}

bool cardputer_modem_save_wifi(const char *ssid, const char *pass) {
    if (!ssid || !pass || ssid[0] == '\0' ||
        strlen(ssid) >= sizeof(wifi_ssid) ||
        strlen(pass) >= sizeof(wifi_pass)) {
        return false;
    }
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result_ssid = nvs_set_str(handle, "wifi_ssid", ssid);
    esp_err_t result_pass = nvs_set_str(handle, "wifi_pass", pass);
    esp_err_t commit = (result_ssid == ESP_OK && result_pass == ESP_OK)
        ? nvs_commit(handle) : ESP_FAIL;
    nvs_close(handle);
    const bool ok = result_ssid == ESP_OK && result_pass == ESP_OK && commit == ESP_OK;
    if (ok) {
        strlcpy(wifi_ssid, ssid, sizeof(wifi_ssid));
        strlcpy(wifi_pass, pass, sizeof(wifi_pass));
        WiFi.disconnect(true);
        delay(20);
        start_wifi();
    }
    return ok;
}

static void start_wifi(void) {
    if (wifi_ssid[0] == '\0') return;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(wifi_ssid, wifi_pass);
    wifi_started = true;
    last_wifi_attempt_ms = millis();
}

static void hangup(void) {
    if (tcp_client.connected()) tcp_client.stop();
    command_mode = true;
    plus_count = 0;
}

static void parse_wifi_command(const char *args) {
    if (args[0] == '?') {
        if (WiFi.status() == WL_CONNECTED) {
            result("WIFI CONNECTED");
            queue_text(WiFi.localIP().toString().c_str());
            queue_text("\r\n");
        } else if (wifi_ssid[0] != '\0') {
            result("WIFI CONFIGURED");
        } else {
            result("WIFI NOT CONFIGURED");
        }
        return;
    }

    result("USE SETTINGS WIFI SCAN");
}

static void dial_tcp(const char *target) {
    if (WiFi.status() != WL_CONNECTED) {
        if (!wifi_started) start_wifi();
        result("NO CARRIER");
        return;
    }

    char host[64] = {};
    uint16_t port = 23;
    const char *colon = strrchr(target, ':');
    if (colon) {
        const size_t host_len = colon - target;
        if (host_len == 0 || host_len >= sizeof(host)) {
            result("ERROR");
            return;
        }
        memcpy(host, target, host_len);
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 23;
    } else {
        if (strlen(target) >= sizeof(host)) {
            result("ERROR");
            return;
        }
        strlcpy(host, target, sizeof(host));
    }

    hangup();
    if (tcp_client.connect(host, port)) {
        command_mode = false;
        result("CONNECT");
    } else {
        result("NO CARRIER");
    }
}

static void parse_command(void) {
    cmd_line[cmd_len] = '\0';
    char *cmd = cmd_line;
    while (*cmd == ' ') cmd++;
    if ((cmd[0] != 'A' && cmd[0] != 'a') || (cmd[1] != 'T' && cmd[1] != 't')) {
        result("ERROR");
        return;
    }
    cmd += 2;
    if (*cmd == '\0') {
        result("OK");
        return;
    }

    while (*cmd) {
        if (*cmd == ' ') {
            cmd++;
            continue;
        }
        const char op = (*cmd >= 'a' && *cmd <= 'z') ? *cmd - 32 : *cmd;
        if (op == 'Z') {
            echo_on = true;
            hangup();
            result("OK");
            return;
        }
        if (op == 'I') {
            result("CardPuter86 Hayes WiFi Modem 0.4");
            result("OK");
            return;
        }
        if (op == 'E') {
            cmd++;
            echo_on = (*cmd != '0');
            if (*cmd == '0' || *cmd == '1') cmd++;
            continue;
        }
        if (op == 'H') {
            hangup();
            result("OK");
            return;
        }
        if (op == 'D') {
            cmd++;
            if (*cmd == 'T' || *cmd == 't' || *cmd == 'P' || *cmd == 'p') cmd++;
            dial_tcp(cmd);
            return;
        }
        if (strncmp(cmd, "+WIFI", 5) == 0 || strncmp(cmd, "+wifi", 5) == 0) {
            parse_wifi_command(cmd + 5);
            return;
        }
        result("ERROR");
        return;
    }
    result("OK");
}

static void command_byte(uint8_t value) {
    if (echo_on) rx_push(value);
    if (value == '\n') return;
    if (value == '\r') {
        parse_command();
        cmd_len = 0;
        return;
    }
    if (value == 8 || value == 127) {
        if (cmd_len > 0) cmd_len--;
        return;
    }
    if (cmd_len < sizeof(cmd_line) - 1) {
        cmd_line[cmd_len++] = (char)value;
    }
}

static void data_byte(uint8_t value) {
    if (value == '+') {
        plus_count++;
        if (plus_count >= 3) {
            command_mode = true;
            plus_count = 0;
            result("OK");
        }
        return;
    }
    while (plus_count > 0) {
        tcp_client.write('+');
        plus_count--;
    }
    if (tcp_client.connected()) {
        tcp_client.write(value);
    } else {
        command_mode = true;
        result("NO CARRIER");
    }
}

void cardputer_modem_init(void) {
    ier = lcr = mcr = scr = 0;
    divisor = 12;
    cmd_len = 0;
    echo_on = true;
    command_mode = true;
    rx_head = rx_tail = 0;
    load_wifi_settings();
    if (wifi_ssid[0] != '\0') start_wifi();
}

void cardputer_modem_poll(void) {
    if (wifi_started && WiFi.status() != WL_CONNECTED &&
        millis() - last_wifi_attempt_ms > 15000) {
        last_wifi_attempt_ms = millis();
        WiFi.disconnect();
        WiFi.begin(wifi_ssid, wifi_pass);
    }
    if (!command_mode && tcp_client.connected()) {
        while (tcp_client.available() && !rx_full()) {
            rx_push((uint8_t)tcp_client.read());
        }
    } else if (!command_mode && !tcp_client.connected()) {
        command_mode = true;
        result("NO CARRIER");
    }
}

bool cardputer_modem_wifi_configured(void) {
    return wifi_ssid[0] != '\0';
}

bool cardputer_modem_wifi_connected(void) {
    return WiFi.status() == WL_CONNECTED;
}

const char *cardputer_modem_wifi_ssid(void) {
    return wifi_ssid;
}

void cardputer_modem_pause_wifi(void) {
    if (tcp_client.connected()) tcp_client.stop();
    command_mode = true;
    wifi_started = false;
    WiFi.disconnect(false, false);
}

void cardputer_modem_resume_wifi(void) {
    if (wifi_ssid[0] != '\0') start_wifi();
}

bool cardputer_modem_port(uint16_t port) {
    return port >= COM1_BASE && port <= COM1_BASE + 7;
}

void cardputer_modem_write(uint16_t port, uint8_t value) {
    const uint8_t reg = port - COM1_BASE;
    if (lcr & 0x80) {
        if (reg == 0) divisor = (divisor & 0xFF00) | value;
        else if (reg == 1) divisor = (divisor & 0x00FF) | ((uint16_t)value << 8);
        return;
    }

    switch (reg) {
        case 0:
            if (command_mode) command_byte(value);
            else data_byte(value);
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

uint8_t cardputer_modem_read(uint16_t port) {
    const uint8_t reg = port - COM1_BASE;
    if (lcr & 0x80) {
        if (reg == 0) return divisor & 0xFF;
        if (reg == 1) return divisor >> 8;
    }

    switch (reg) {
        case 0: {
            const int value = rx_pop();
            return value < 0 ? 0 : (uint8_t)value;
        }
        case 1:
            return ier;
        case 2:
            return rx_empty() ? 0x01 : 0x04;
        case 3:
            return lcr;
        case 4:
            return mcr;
        case 5:
            return (rx_empty() ? 0 : UART_LSR_DR) | UART_LSR_THRE | UART_LSR_TEMT;
        case 6:
            return UART_MSR_CTS | UART_MSR_DSR |
                (tcp_client.connected() ? UART_MSR_DCD : 0);
        case 7:
            return scr;
        default:
            return 0xFF;
    }
}
