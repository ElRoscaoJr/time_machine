#pragma once
#include <stddef.h>
typedef enum { PANEL_OFFLINE, PANEL_HOTSPOT, PANEL_LAN } panel_connection_t;
void network_start(void);
void network_setup(void);
void network_address(char *buffer, size_t size);
panel_connection_t network_panel_connection(char ip[16], char ssid[33]);
