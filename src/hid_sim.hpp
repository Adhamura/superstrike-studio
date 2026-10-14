// Demo mouse: a simulated PRO X3 SUPERSTRIKE that answers HID++ 2.0 requests like the real one
// (receiver slot 1, five onboard profiles, HITS, DPI, report rate). Every part of the app runs
// unchanged on top of it; writes go to memory and are lost when the app quits.
// On with --demo or SUPERSTRIKE_DEMO=1. Used for trying the app without the mouse, the README
// screenshots and the CI smoke test.
#pragma once
#include "hidpp.hpp"

constexpr const char* SIM_PATH = "demo";

void sim_enable();
bool sim_enabled();  // also true when SUPERSTRIKE_DEMO=1 is set
// One HID++ report in, the reply reports out (empty when the request is not for the demo mouse).
std::vector<Bytes> sim_request(const Bytes& report);
