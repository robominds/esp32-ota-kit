// The public API. See ota.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include "ota.h"

#include <Arduino.h>

#include "detail/pull.h"
#include "detail/push.h"
#include "detail/rollback.h"

// The core defines a weak verifyRollbackLater() returning false, which makes
// it confirm every image right after setup(). Returning true hands that
// decision to detail::rollback. It must stay in THIS file: PlatformIO links
// libraries as archives, and an object file nothing else references is never
// linked, so an override kept elsewhere would silently disable rollback. The
// core declares it only in a C file, so it needs C linkage.
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {
namespace {

bool g_begun        = false;
bool g_network_seen = false;

}  // namespace

void begin(const Config& config, Observer& observer) {
    if (g_begun) return;
    g_begun = true;
    detail::rollback::begin(observer, config.validate_after_network_ms,
                            config.validate_timeout_ms);
    detail::push::configure(config, observer);
    detail::pull::configure(config, observer);
}

void setNetworkUp(bool up) {
    if (!g_begun) return;
    const uint32_t now = millis();
    if (up && !g_network_seen) {
        g_network_seen = true;
        detail::push::start();
        detail::rollback::networkUp(now);
    }
    detail::pull::setNetworkUp(up, now);
}

void poll() {
    if (!g_begun) return;
    detail::rollback::poll(millis());
    detail::push::poll();
    detail::pull::poll(millis());
}

void requestCheck() {
    if (g_begun) detail::pull::requestCheck();
}

void requestInstall() {
    if (g_begun) detail::pull::requestInstall();
}

bool busy() { return g_begun && (detail::push::busy() || detail::pull::busy()); }

}  // namespace ota
