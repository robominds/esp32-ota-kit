// See pull.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include "detail/pull.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <Update.h>

#include <algorithm>
#include <string>

#include "detail/rollback.h"
#include "detail/timing.h"
#include "otacore.h"

namespace ota {
namespace detail {
namespace pull {
namespace {

constexpr int      HTTP_TIMEOUT_MS    = 5000;

// A manifest is four short fields. Anything bigger is a wrong URL (for example
// one pointing at firmware.bin), not a manifest.
constexpr int      MANIFEST_MAX_BYTES = 1024;

// Update.writeStream() is not used: with the server gone it retries a 5 s read
// 300 times (about 25 minutes blocked), and its first-byte check peeks before
// the body has arrived. download() reads the body itself instead.
constexpr uint32_t STALL_TIMEOUT_MS   = 10000;
constexpr size_t   CHUNK_BYTES        = 4096;

Config            g_config;
Observer*         g_observer        = nullptr;
bool              g_network_up      = false;
bool              g_network_seen    = false;
bool              g_check_pending   = false;
uint32_t          g_check_at_ms     = 0;
bool              g_install_pending = false;
bool              g_busy            = false;
bool              g_available       = false;
otacore::Manifest g_manifest;
ProgressThrottle  g_throttle;

void checkFailed(const char* message) {
    g_busy = false;
    Serial.printf("ota: check failed: %s\n", message);
    const CheckResult result{CheckResult::Failed, nullptr, 0, message};
    g_observer->onCheckResult(result);
}

void installFailed(const char* message) {
    g_busy      = false;
    g_available = false;   // a new check is required before another install
    Serial.printf("ota: install failed: %s\n", message);
    g_observer->onError(Source::Pull, message);
}

// Describes an HTTPClient result other than 200 OK.
void httpDetail(int code, char* out, size_t len) {
    if (code < 0) {
        snprintf(out, len, "%s", HTTPClient::errorToString(code).c_str());
    } else {
        snprintf(out, len, "HTTP %d", code);
    }
}

// Streams exactly `total` body bytes into Update, reporting progress. Returns
// nullptr on success, otherwise why it stopped.
const char* download(NetworkClient& stream, size_t total) {
    static uint8_t buf[CHUNK_BYTES];   // static: the loop task's stack is small
    size_t   done         = 0;
    uint32_t last_data_ms = millis();
    while (done < total) {
        if (millis() - last_data_ms >= STALL_TIMEOUT_MS) return "download stalled";
        const int avail = stream.available();
        if (avail <= 0) {
            if (!stream.connected()) return "connection lost";
            delay(1);
            continue;
        }
        const size_t want =
            std::min({static_cast<size_t>(avail), CHUNK_BYTES, total - done});
        const int got = stream.read(buf, want);
        if (got <= 0) {
            delay(1);
            continue;
        }
        if (Update.write(buf, static_cast<size_t>(got)) != static_cast<size_t>(got)) {
            return Update.hasError() ? Update.errorString() : "flash write failed";
        }
        done += static_cast<size_t>(got);
        last_data_ms = millis();
        const uint8_t percent = percentOf(done, total);
        if (g_throttle.shouldEmit(last_data_ms, percent)) {
            g_observer->onProgress(Source::Pull, percent);
        }
    }
    return nullptr;
}

void runCheck() {
    g_busy      = true;
    g_available = false;
    g_observer->onCheckStarted();

    if (!g_network_up) {
        checkFailed("no network");
        return;
    }
    otacore::Version running;
    if (!otacore::parseVersion(g_config.running_version, running)) {
        checkFailed("running_version is not MAJOR.MINOR.PATCH");
        return;
    }

    NetworkClient client;
    HTTPClient    http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, g_config.manifest_url)) {
        checkFailed("bad manifest_url");
        return;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char detail[64];
        httpDetail(code, detail, sizeof detail);
        http.end();
        checkFailed(detail);
        return;
    }
    if (http.getSize() > MANIFEST_MAX_BYTES) {
        http.end();
        checkFailed("manifest too large");
        return;
    }
    const String body = http.getString();
    http.end();
    if (body.length() > static_cast<unsigned>(MANIFEST_MAX_BYTES)) {
        checkFailed("manifest too large");
        return;
    }

    std::string error;
    otacore::Manifest manifest;
    if (!otacore::parseManifest(body.c_str(), body.length(), manifest, error)) {
        checkFailed(error.c_str());
        return;
    }

    otacore::Version server;
    otacore::parseVersion(manifest.version.c_str(), server);   // validated by parseManifest
    const bool newer = otacore::compareVersions(running, server) < 0;
    Serial.printf("ota: server %s, running %s -> %s\n", manifest.version.c_str(),
                  g_config.running_version, newer ? "update available" : "up to date");

    g_busy = false;
    if (newer) {
        g_manifest  = manifest;
        g_available = true;
        const CheckResult result{CheckResult::Available, g_manifest.version.c_str(),
                                 g_manifest.size, nullptr};
        g_observer->onCheckResult(result);
    } else {
        const CheckResult result{CheckResult::UpToDate, manifest.version.c_str(), 0, nullptr};
        g_observer->onCheckResult(result);
    }
}

void runInstall() {
    if (!g_available) return;
    g_busy = true;
    g_throttle.reset();
    g_observer->onTransferStarted(Source::Pull);

    if (!g_network_up) {
        installFailed("no network");
        return;
    }

    NetworkClient client;
    HTTPClient    http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, g_manifest.url.c_str())) {
        installFailed("bad image url");
        return;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char detail[64];
        httpDetail(code, detail, sizeof detail);
        http.end();
        installFailed(detail);
        return;
    }
    const int len = http.getSize();
    if (len <= 0 || static_cast<size_t>(len) != g_manifest.size) {
        http.end();
        installFailed("size differs from manifest");
        return;
    }
    NetworkClient* stream = http.getStreamPtr();
    if (stream == nullptr) {
        http.end();
        installFailed("connection lost");
        return;
    }

    // Writing starts below and overwrites the slot with the last confirmed image.
    rollback::confirmBeforeUpdate();
    if (!Update.begin(static_cast<size_t>(len))) {
        const char* why = Update.errorString();
        http.end();
        installFailed(why);
        return;
    }
    Update.setMD5(g_manifest.md5.c_str());   // cannot fail: parseManifest checked 32 hex chars

    const char* why = download(*stream, static_cast<size_t>(len));
    http.end();
    if (why == nullptr && !Update.end()) {
        why = Update.hasError() ? Update.errorString() : "image not accepted";
    }
    if (why != nullptr) {
        Update.abort();
        installFailed(why);
        return;
    }

    Serial.println("ota: installed, rebooting");
    if (g_throttle.shouldEmit(millis(), 100)) g_observer->onProgress(Source::Pull, 100);
    g_observer->onRebooting(Source::Pull);
    delay(1000);
    ESP.restart();
}

}  // namespace

void configure(const Config& config, Observer& observer) {
    g_config   = config;
    g_observer = &observer;
}

bool enabled() { return g_observer != nullptr && g_config.manifest_url != nullptr; }

void setNetworkUp(bool up, uint32_t now_ms) {
    g_network_up = up;
    if (!up || g_network_seen) return;
    g_network_seen = true;
    if (enabled() && g_config.auto_check_delay_ms > 0) {
        g_check_pending = true;
        g_check_at_ms   = now_ms + g_config.auto_check_delay_ms;
    }
}

void requestCheck() {
    if (!enabled() || g_busy) return;
    g_check_pending = true;
    g_check_at_ms   = millis();
}

void requestInstall() {
    if (!enabled() || g_busy || !g_available) return;
    g_install_pending = true;
}

void poll(uint32_t now_ms) {
    if (!enabled()) return;
    if (g_install_pending) {
        g_install_pending = false;
        runInstall();
        return;
    }
    if (g_check_pending && static_cast<int32_t>(now_ms - g_check_at_ms) >= 0) {
        g_check_pending = false;
        runCheck();
    }
}

bool busy() { return g_busy; }

}  // namespace pull
}  // namespace detail
}  // namespace ota
