/*
 * guest_ui.h - the guest's windows, controls and dialog items as JSON
 * (screen coordinates), read from guest memory. See guest_ui.cpp.
 */
#pragma once

#include <string>

std::string guest_ui_json(bool *has_front = nullptr);
