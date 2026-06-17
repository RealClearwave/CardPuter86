#ifndef CARDPUTER_MODEM_H
#define CARDPUTER_MODEM_H

#include <Arduino.h>

static const uint8_t CARDPUTER_MODEM_MAX_SCAN = 12;

struct CardputerWifiNetwork {
    char ssid[33];
    int32_t rssi;
    bool encrypted;
};

void cardputer_modem_init(void);
void cardputer_modem_poll(void);
void cardputer_modem_install_bda(void);
const char *cardputer_modem_ssid(void);
bool cardputer_modem_has_password(void);
bool cardputer_modem_set_wifi(const char *ssid, const char *password);
bool cardputer_modem_set_ssid(const char *ssid);
bool cardputer_modem_set_password(const char *password);
bool cardputer_modem_clear_wifi(void);
bool cardputer_modem_connect_saved(uint32_t timeout_ms);
bool cardputer_modem_wifi_connected(void);
int cardputer_modem_scan_networks(CardputerWifiNetwork *networks, uint8_t max_networks);
bool cardputer_modem_handles_port(uint16_t portnum);
void cardputer_modem_port_out(uint16_t portnum, uint8_t value);
uint8_t cardputer_modem_port_in(uint16_t portnum);

#endif
