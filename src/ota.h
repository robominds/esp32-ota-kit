// esp32-ota-kit: WiFi firmware updates with bootloader rollback.
//
// Push: ArduinoOTA, for `pio run -e <env>_ota -t upload` (espota).
// Pull: an HTTP manifest check and an MD5-verified, bounded download.
// Rollback: a new image is confirmed only after it has run normally for a
// while; a build that crashes first is reverted by the bootloader.
//
// The library draws nothing. It reports through an Observer, and the
// application owns WiFi. See README.md for the full contract.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#pragma once

#include <cstddef>
#include <cstdint>

namespace ota {

// Strings must outlive the program; build-flag literals do.
struct Config {
    const char* hostname        = nullptr;   // mDNS and ArduinoOTA name, without ".local"; required for push
    const char* push_password   = nullptr;   // ArduinoOTA password; nullptr disables push
    uint16_t    push_port       = 3232;
    const char* manifest_url    = nullptr;   // http URL of manifest.json; nullptr disables pull
    const char* running_version = nullptr;   // this build's "MAJOR.MINOR.PATCH"
    uint32_t    auto_check_delay_ms       = 5000;    // after first network up; 0 = never
    uint32_t    validate_after_network_ms = 30000;
    uint32_t    validate_timeout_ms       = 90000;   // from boot, whether or not the network came up
};

enum class Source { Push, Pull };

struct SlotInfo {
    const char* label;   // "app0" / "app1"
    const char* state;   // "serial-flashed", "pending verify", "confirmed", ...
    uint32_t    boots;   // NVS boot counter, 0 if NVS is unavailable
};

struct CheckResult {
    enum Kind { UpToDate, Available, Failed } kind;
    const char* version;   // server version for UpToDate / Available, else nullptr
    size_t      size;      // image size for Available, else 0
    const char* message;   // failure text for Failed, else nullptr
};

// Every method runs on the loop task, from inside begin() or poll(), and may
// update widgets and call lv_timer_handler(). Strings are valid only during
// the call. Do not call back into ota:: except busy(), and requestCheck() /
// requestInstall() from input callbacks that lv_timer_handler() runs while an
// observer repaints (they only queue work).
class Observer {
public:
    virtual ~Observer() = default;
    virtual void onSlot(const SlotInfo&) {}             // after begin(), and when confirmed
    virtual void onPushListening(uint16_t /*port*/) {}
    virtual void onCheckStarted() {}
    virtual void onCheckResult(const CheckResult&) {}
    virtual void onTransferStarted(Source) {}
    virtual void onProgress(Source, uint8_t /*percent*/) {}   // at most 10 per second, and at 100
    virtual void onRebooting(Source) {}                 // just before the restart
    virtual void onError(Source, const char* /*message*/) {}
};

// Once, in setup(), after the display is up. Calls to other functions before
// begin() are ignored.
void begin(const Config& config, Observer& observer);

// Every loop (or on every change). The first `true` starts push, the
// confirmation delay and the automatic check.
void setNetworkUp(bool up);

// Every loop, from loop() - never from inside an LVGL callback or
// lv_timer_handler(). Transfers block here until they finish or fail.
void poll();

// Safe from anywhere on the loop task, including LVGL callbacks: they only
// queue work for poll(). Ignored while busy() or when pull is disabled;
// requestInstall() also needs the last check to have returned Available.
void requestCheck();
void requestInstall();

bool busy();

}  // namespace ota
