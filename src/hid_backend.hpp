// Raw HID primitives, one implementation per OS (hidpp_linux.cpp, hidpp_win.cpp).
// Only hidpp.cpp uses them; it builds the HID++ frames on top.
#pragma once
#include "hidpp.hpp"

bool hid_long_only();                            // true: the node takes long reports only (Windows)
unsigned hid_process_id();                       // picks the software id of this process
HidDev* hid_open(const std::string& path);       // throws HidppError
void hid_close(HidDev* d);
void hid_write(HidDev* d, const Bytes& report);  // one whole report; throws HidppError
Bytes hid_read(HidDev* d, int timeout_ms);       // one report; empty when nothing came in time
