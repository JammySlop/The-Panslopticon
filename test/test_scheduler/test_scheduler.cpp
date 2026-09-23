// Contract tests for AppScheduler::claimIfDue.
//
// These assert the invariants that hold for ANY sane scheduling policy. They
// deliberately do not pin down the drift strategy (reset-to-now vs. advance-by-
// interval) so that either remains a legal implementation.
#include <unity.h>

#include "AppScheduler.h"

namespace {

int runCount = 0;
void noop() { ++runCount; }

app::PeriodicTask makeTask(uint32_t intervalMs, uint32_t anchorMs) {
    return app::PeriodicTask{"t", intervalMs, noop, anchorMs, 0};
}

}  // namespace

void setUp() { runCount = 0; }
void tearDown() {}

void test_not_due_before_interval_elapses() {
    app::PeriodicTask t = makeTask(100, 0);
    TEST_ASSERT_FALSE(app::AppScheduler::claimIfDue(t, 0));
    TEST_ASSERT_FALSE(app::AppScheduler::claimIfDue(t, 99));
}

void test_due_once_interval_elapses() {
    app::PeriodicTask t = makeTask(100, 0);
    TEST_ASSERT_TRUE(app::AppScheduler::claimIfDue(t, 100));
}

void test_does_not_refire_immediately_after_claiming() {
    app::PeriodicTask t = makeTask(100, 0);
    TEST_ASSERT_TRUE(app::AppScheduler::claimIfDue(t, 100));
    TEST_ASSERT_FALSE(app::AppScheduler::claimIfDue(t, 100));
    TEST_ASSERT_FALSE(app::AppScheduler::claimIfDue(t, 150));
}

void test_fires_at_expected_rate_over_time() {
    app::PeriodicTask t = makeTask(100, 0);
    int fired = 0;
    for (uint32_t now = 0; now <= 1000; now += 10) {
        if (app::AppScheduler::claimIfDue(t, now)) ++fired;
    }
    // 10 intervals in 1000 ms, sampled finely enough to catch each one.
    TEST_ASSERT_EQUAL_INT(10, fired);
}

void test_survives_millis_rollover() {
    const uint32_t nearMax = 0xFFFFFF00u;
    app::PeriodicTask t = makeTask(100, nearMax);
    // 0xFFFFFF00 + 0x100 wraps to 0. The elapsed time is exactly 256 ms.
    TEST_ASSERT_TRUE(app::AppScheduler::claimIfDue(t, 0x00000000u));
}

void test_counts_missed_deadlines_when_starved() {
    app::PeriodicTask t = makeTask(100, 0);
    // The loop stalled for half a second; at least one deadline was missed.
    TEST_ASSERT_TRUE(app::AppScheduler::claimIfDue(t, 500));
    TEST_ASSERT_GREATER_THAN_UINT32(0, t.missedDeadlines);
}

void test_scheduler_begin_prevents_immediate_fire() {
    app::PeriodicTask table[] = {makeTask(100, 0)};
    app::AppScheduler s(table, 1);
    s.begin(12345);
    s.tick(12345);
    TEST_ASSERT_EQUAL_INT(0, runCount);
    s.tick(12445);
    TEST_ASSERT_EQUAL_INT(1, runCount);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_not_due_before_interval_elapses);
    RUN_TEST(test_due_once_interval_elapses);
    RUN_TEST(test_does_not_refire_immediately_after_claiming);
    RUN_TEST(test_fires_at_expected_rate_over_time);
    RUN_TEST(test_survives_millis_rollover);
    RUN_TEST(test_counts_missed_deadlines_when_starved);
    RUN_TEST(test_scheduler_begin_prevents_immediate_fire);
    return UNITY_END();
}
