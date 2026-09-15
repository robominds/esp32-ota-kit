// Bootloader rollback contract: slot info, boot counter, and when the running
// image is confirmed (esp_ota_mark_app_valid_cancel_rollback).
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#pragma once

#include <cstdint>

#include "ota.h"

namespace ota {
namespace detail {
namespace rollback {

// Increments the NVS boot counter, reads the running slot and its OTA state,
// logs them and reports onSlot. A serial-flashed or already valid image counts
// as confirmed.
void begin(Observer& observer, uint32_t after_network_ms, uint32_t timeout_ms);

// The first call starts the confirmation delay.
void networkUp(uint32_t now_ms);

// Confirms the image once the delay or the boot timeout has passed.
void poll(uint32_t now_ms);

// Confirms now if still unconfirmed. Call just before a transfer writes the
// other slot: it holds the last confirmed image, so the running one must be
// confirmed first or a failing update would leave nothing valid to roll back to.
void confirmBeforeUpdate();

bool confirmed();

}  // namespace rollback
}  // namespace detail
}  // namespace ota
