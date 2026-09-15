// Push OTA: ArduinoOTA, which PlatformIO's espota upload talks to.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#pragma once

#include "ota.h"

namespace ota {
namespace detail {
namespace push {

// Stores the configuration. Does no network work.
void configure(const Config& config, Observer& observer);

// Starts ArduinoOTA (and mDNS) on the first network up. No-op when push is
// disabled (no password) or already started.
void start();

// Services ArduinoOTA. A transfer blocks here until it ends; ArduinoOTA
// reboots on success.
void poll();

bool busy();

}  // namespace push
}  // namespace detail
}  // namespace ota
