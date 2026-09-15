// Pull OTA: fetch a manifest, compare versions, and install a newer image with
// an MD5-verified download that gives up after 10 s without data.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#pragma once

#include <cstdint>

#include "ota.h"

namespace ota {
namespace detail {
namespace pull {

// Stores the configuration. Does no network work.
void configure(const Config& config, Observer& observer);

// True when a manifest URL was configured.
bool enabled();

// Tracks the network. The first `up` schedules the automatic check.
void setNetworkUp(bool up, uint32_t now_ms);

// Queue work for poll(). Ignored when disabled or busy; requestInstall() also
// needs the last check to have returned Available.
void requestCheck();
void requestInstall();

// Runs a queued install first, otherwise a due check. Both block while they run.
void poll(uint32_t now_ms);

bool busy();

}  // namespace pull
}  // namespace detail
}  // namespace ota
