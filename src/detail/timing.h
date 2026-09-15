// Pure timing helpers shared by the rollback and transfer code. No Arduino
// headers, so they are host-tested (test/test_timing).
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#pragma once

#include <cstddef>
#include <cstdint>

namespace ota {
namespace detail {

// Decides when a freshly installed image has run long enough to be confirmed:
// after_network_ms after the network FIRST came up, or timeout_ms after boot.
// A later network drop does not restart the delay: the point is to prove the
// new build runs normally for a while, and a drop does not make it less proven.
class ConfirmTimer {
public:
    void configure(uint32_t after_network_ms, uint32_t timeout_ms) {
        after_   = after_network_ms;
        timeout_ = timeout_ms;
    }

    // Records the first time the network came up. True only on that first call.
    bool networkUp(uint32_t now_ms) {
        if (seen_) return false;
        seen_ = true;
        at_   = now_ms;
        return true;
    }

    // now_ms - at_ is unsigned, so the network delay survives millis() wrap.
    // timeout_ms is measured from boot (millis() == 0).
    bool due(uint32_t now_ms) const {
        return (seen_ && now_ms - at_ >= after_) || now_ms >= timeout_;
    }

    uint32_t afterNetworkMs() const { return after_; }

private:
    uint32_t after_   = 30000;
    uint32_t timeout_ = 90000;
    bool     seen_    = false;
    uint32_t at_      = 0;
};

// Percentage of total that done represents, 0..100. 0 when total is 0.
inline uint8_t percentOf(size_t done, size_t total) {
    if (total == 0) return 0;
    if (done >= total) return 100;
    return static_cast<uint8_t>((static_cast<uint64_t>(done) * 100U) / total);
}

// Limits progress reports to one per interval, while never dropping the first
// report or 100 %, and never repeating the same percentage.
class ProgressThrottle {
public:
    explicit ProgressThrottle(uint32_t interval_ms = 100) : interval_(interval_ms) {}

    void reset() {
        started_      = false;
        last_percent_ = kNone;
    }

    bool shouldEmit(uint32_t now_ms, uint8_t percent) {
        if (started_ && percent == last_percent_) return false;
        if (started_ && percent != 100 && now_ms - last_ms_ < interval_) return false;
        started_      = true;
        last_ms_      = now_ms;
        last_percent_ = percent;
        return true;
    }

private:
    static constexpr uint8_t kNone = 255;

    uint32_t interval_;
    uint32_t last_ms_      = 0;
    uint8_t  last_percent_ = kNone;
    bool     started_      = false;
};

}  // namespace detail
}  // namespace ota
