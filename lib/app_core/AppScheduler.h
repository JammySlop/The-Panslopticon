// AppScheduler — a cooperative, non-blocking periodic task runner.
//
// Deliberately free of any Arduino dependency: the caller supplies "now" in
// milliseconds. That keeps the scheduling logic compilable and testable on a
// host machine, with no board attached.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace app {

using TaskFn = void (*)();

struct PeriodicTask {
    const char* name;
    uint32_t intervalMs;
    TaskFn run;

    // Schedule anchor: the reference point the next deadline is measured from.
    uint32_t lastRunMs;
    // Incremented whenever a task is dispatched a full interval or more late.
    uint32_t missedDeadlines;
};

class AppScheduler {
  public:
    AppScheduler(PeriodicTask* tasks, size_t count);

    // Anchors every task so none fire immediately on the first tick().
    void begin(uint32_t nowMs);

    // Runs every task that is due. Call as often as possible from loop().
    void tick(uint32_t nowMs);

    // Decides whether `task` is due at `nowMs`. When it returns true it must
    // also advance task.lastRunMs to the anchor for the following interval,
    // and bump task.missedDeadlines if the dispatch is a full interval late.
    //
    // Must be correct across the uint32_t millisecond rollover (~49.7 days).
    static bool claimIfDue(PeriodicTask& task, uint32_t nowMs);

    size_t count() const { return count_; }
    const PeriodicTask& at(size_t i) const { return tasks_[i]; }

  private:
    PeriodicTask* tasks_;
    size_t count_;
};

}  // namespace app
