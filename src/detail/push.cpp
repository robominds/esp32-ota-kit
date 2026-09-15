// See push.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include "detail/push.h"

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "detail/rollback.h"
#include "detail/timing.h"

namespace ota {
namespace detail {
namespace push {
namespace {

Config           g_config;
Observer*        g_observer = nullptr;
bool             g_started  = false;
bool             g_busy     = false;
ProgressThrottle g_throttle;

const char* errorText(ota_error_t e) {
    switch (e) {
        case OTA_AUTH_ERROR:    return "auth failed";
        case OTA_BEGIN_ERROR:   return "begin failed (image too large?)";
        case OTA_CONNECT_ERROR: return "connect failed";
        case OTA_RECEIVE_ERROR: return "receive failed";
        case OTA_END_ERROR:     return "end failed (bad image)";
        default:                return "unknown error";
    }
}

}  // namespace

void configure(const Config& config, Observer& observer) {
    g_config   = config;
    g_observer = &observer;
}

void start() {
    if (g_started || g_observer == nullptr || g_config.push_password == nullptr) return;
    g_started = true;

    ArduinoOTA.setPort(g_config.push_port);
    ArduinoOTA.setHostname(g_config.hostname);
    ArduinoOTA.setPassword(g_config.push_password);
    ArduinoOTA.setRebootOnSuccess(true);

    // ArduinoOTA calls onStart only after authentication succeeded, and before
    // the first byte is written to flash.
    ArduinoOTA.onStart([]() {
        rollback::confirmBeforeUpdate();
        g_busy = true;
        g_throttle.reset();
        Serial.println("ota: push start");
        g_observer->onTransferStarted(Source::Push);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        const uint8_t percent = percentOf(done, total);
        if (g_throttle.shouldEmit(millis(), percent)) {
            g_observer->onProgress(Source::Push, percent);
        }
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("ota: push done, rebooting");
        if (g_throttle.shouldEmit(millis(), 100)) g_observer->onProgress(Source::Push, 100);
        g_observer->onRebooting(Source::Push);
        delay(500);   // let the observer's last frame reach the display
    });
    ArduinoOTA.onError([](ota_error_t e) {
        g_busy = false;
        const char* text = errorText(e);
        Serial.printf("ota: push error %d (%s)\n", static_cast<int>(e), text);
        g_observer->onError(Source::Push, text);
    });

    ArduinoOTA.begin();
    Serial.printf("ota: push listening on %s.local:%u\n", g_config.hostname,
                  static_cast<unsigned>(g_config.push_port));
    g_observer->onPushListening(g_config.push_port);
}

void poll() {
    if (g_started) ArduinoOTA.handle();
}

bool busy() { return g_busy; }

}  // namespace push
}  // namespace detail
}  // namespace ota
