#ifndef CARDPUTER_MODEM_H
#define CARDPUTER_MODEM_H

#include <Arduino.h>

void cardputer_modem_init(void);
void cardputer_modem_poll(void);
bool cardputer_modem_save_wifi(const char *ssid, const char *pass);
bool cardputer_modem_wifi_configured(void);
bool cardputer_modem_wifi_connected(void);
const char *cardputer_modem_wifi_ssid(void);
bool cardputer_modem_port(uint16_t port);
void cardputer_modem_write(uint16_t port, uint8_t value);
uint8_t cardputer_modem_read(uint16_t port);

#endif
