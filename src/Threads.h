#pragma once
#include <atomic>
#include <string>

extern std::atomic<bool> g_running;

void socket_listener();                                   // control port 8080
void canbus_listener(bool debugprint, std::string node);  // OBD-II ECU on 0x7DF/0x7E8
void telltale_broadcaster(std::string node);              // 0x420 every 100 ms
void bms_ecu(std::string node);                           // UDS 0x22 over ISO-TP, 0x7E4 -> 0x7EC
