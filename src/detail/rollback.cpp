// See rollback.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include "detail/rollback.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

#include <cstring>

#include "detail/timing.h"

namespace ota {
namespace detail {
namespace rollback {
namespace {

Observer*    g_observer  = nullptr;
ConfirmTimer g_timer;
bool         g_confirmed = false;
uint32_t     g_boots     = 0;
char         g_label[8]  = "app?";

const char* stateText(esp_ota_img_states_t s) {
    switch (s) {
        case ESP_OTA_IMG_NEW:            return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending verify";
        case ESP_OTA_IMG_VALID:          return "confirmed";
        case ESP_OTA_IMG_INVALID:        return "invalid";
        case ESP_OTA_IMG_ABORTED:        return "aborted";
        case ESP_OTA_IMG_UNDEFINED:
        default:                         return "serial-flashed";
    }
}

esp_ota_img_states_t currentState() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t s = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr) esp_ota_get_state_partition(running, &s);
    return s;
}

void report() {
    if (g_observer == nullptr) return;
    const SlotInfo info{g_label, stateText(currentState()), g_boots};
    g_observer->onSlot(info);
}

void confirm(const char* when) {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    g_confirmed = true;
    Serial.printf("ota: image confirmed%s (%s)\n", when, esp_err_to_name(err));
    report();
}

}  // namespace

void begin(Observer& observer, uint32_t after_network_ms, uint32_t timeout_ms) {
    g_observer = &observer;
    g_timer.configure(after_network_ms, timeout_ms);

    Preferences prefs;
    if (prefs.begin("ota", false)) {
        g_boots = prefs.getUInt("boots", 0) + 1;
        prefs.putUInt("boots", g_boots);
        prefs.end();
    } else {
        Serial.println("ota: NVS namespace \"ota\" unavailable; boot counter disabled");
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running != nullptr) {
        strlcpy(g_label, running->label, sizeof g_label);
    }
    const esp_ota_img_states_t s = currentState();
    // A serial-flashed image is not pending; nothing to confirm.
    g_confirmed = (s != ESP_OTA_IMG_PENDING_VERIFY && s != ESP_OTA_IMG_NEW);

    Serial.printf("ota: running %s, state %s, boot #%lu\n", g_label, stateText(s),
                  static_cast<unsigned long>(g_boots));
    report();
}

void networkUp(uint32_t now_ms) {
    if (!g_timer.networkUp(now_ms) || g_confirmed) return;
    Serial.printf("ota: network up, confirming in %lu s\n",
                  static_cast<unsigned long>(g_timer.afterNetworkMs() / 1000));
}

void poll(uint32_t now_ms) {
    if (g_confirmed || !g_timer.due(now_ms)) return;
    confirm("");
}

void confirmBeforeUpdate() {
    if (!g_confirmed) confirm(" before update");
}

bool confirmed() { return g_confirmed; }

}  // namespace rollback
}  // namespace detail
}  // namespace ota
