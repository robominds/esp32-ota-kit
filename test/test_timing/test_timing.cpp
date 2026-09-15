// Host tests for detail/timing.h: when an image is due for confirmation, and
// how transfer progress is throttled.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include <unity.h>

#include "detail/timing.h"

using ota::detail::ConfirmTimer;
using ota::detail::ProgressThrottle;
using ota::detail::percentOf;

void setUp(void) {}
void tearDown(void) {}

static ConfirmTimer timer() {
    ConfirmTimer t;
    t.configure(30000, 90000);
    return t;
}

// --- ConfirmTimer ----------------------------------------------------------

static void test_confirm_not_due_before_network_or_timeout(void) {
    const ConfirmTimer t = timer();
    TEST_ASSERT_FALSE(t.due(0));
    TEST_ASSERT_FALSE(t.due(89999));
}

static void test_confirm_due_at_timeout_without_network(void) {
    const ConfirmTimer t = timer();
    TEST_ASSERT_TRUE(t.due(90000));
}

static void test_confirm_due_after_network_delay(void) {
    ConfirmTimer t = timer();
    TEST_ASSERT_TRUE(t.networkUp(5000));
    TEST_ASSERT_FALSE(t.due(34999));
    TEST_ASSERT_TRUE(t.due(35000));
}

static void test_confirm_first_network_up_wins(void) {
    ConfirmTimer t = timer();
    TEST_ASSERT_TRUE(t.networkUp(5000));
    TEST_ASSERT_FALSE(t.networkUp(20000));
    TEST_ASSERT_TRUE(t.due(35000));
}

static void test_confirm_reports_configured_delay(void) {
    const ConfirmTimer t = timer();
    TEST_ASSERT_EQUAL_UINT32(30000, t.afterNetworkMs());
}

// --- percentOf -------------------------------------------------------------

static void test_percent_zero_total_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, percentOf(5, 0));
}

static void test_percent_basic_values(void) {
    TEST_ASSERT_EQUAL_UINT8(0, percentOf(0, 200));
    TEST_ASSERT_EQUAL_UINT8(50, percentOf(100, 200));
    TEST_ASSERT_EQUAL_UINT8(99, percentOf(199, 200));
    TEST_ASSERT_EQUAL_UINT8(100, percentOf(200, 200));
    TEST_ASSERT_EQUAL_UINT8(100, percentOf(250, 200));
}

static void test_percent_firmware_sized(void) {
    TEST_ASSERT_EQUAL_UINT8(50, percentOf(687208, 1374416));
}

// --- ProgressThrottle ------------------------------------------------------

static void test_throttle_first_report_emits(void) {
    ProgressThrottle p;
    TEST_ASSERT_TRUE(p.shouldEmit(1000, 0));
}

static void test_throttle_suppresses_within_interval(void) {
    ProgressThrottle p;
    TEST_ASSERT_TRUE(p.shouldEmit(1000, 0));
    TEST_ASSERT_FALSE(p.shouldEmit(1050, 3));
    TEST_ASSERT_TRUE(p.shouldEmit(1100, 5));
}

static void test_throttle_always_emits_100(void) {
    ProgressThrottle p;
    TEST_ASSERT_TRUE(p.shouldEmit(1000, 10));
    TEST_ASSERT_TRUE(p.shouldEmit(1010, 100));
}

static void test_throttle_never_repeats_a_percent(void) {
    ProgressThrottle p;
    TEST_ASSERT_TRUE(p.shouldEmit(1000, 10));
    TEST_ASSERT_FALSE(p.shouldEmit(2000, 10));
}

static void test_throttle_reset_starts_over(void) {
    ProgressThrottle p;
    TEST_ASSERT_TRUE(p.shouldEmit(1000, 10));
    p.reset();
    TEST_ASSERT_TRUE(p.shouldEmit(1001, 10));
}

static void test_throttle_repeated_100_suppressed(void) {
    ProgressThrottle p;
    TEST_ASSERT_TRUE(p.shouldEmit(1000, 100));
    TEST_ASSERT_FALSE(p.shouldEmit(1500, 100));
}

static void test_confirm_network_after_timeout_is_due(void) {
    ConfirmTimer t = timer();
    TEST_ASSERT_TRUE(t.networkUp(95000));
    TEST_ASSERT_TRUE(t.due(95000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_confirm_not_due_before_network_or_timeout);
    RUN_TEST(test_confirm_due_at_timeout_without_network);
    RUN_TEST(test_confirm_due_after_network_delay);
    RUN_TEST(test_confirm_first_network_up_wins);
    RUN_TEST(test_confirm_reports_configured_delay);
    RUN_TEST(test_percent_zero_total_is_zero);
    RUN_TEST(test_percent_basic_values);
    RUN_TEST(test_percent_firmware_sized);
    RUN_TEST(test_throttle_first_report_emits);
    RUN_TEST(test_throttle_suppresses_within_interval);
    RUN_TEST(test_throttle_always_emits_100);
    RUN_TEST(test_throttle_never_repeats_a_percent);
    RUN_TEST(test_throttle_reset_starts_over);
    RUN_TEST(test_throttle_repeated_100_suppressed);
    RUN_TEST(test_confirm_network_after_timeout_is_due);
    return UNITY_END();
}
