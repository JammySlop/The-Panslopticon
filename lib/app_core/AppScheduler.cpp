#include "AppScheduler.h"

namespace app {

AppScheduler::AppScheduler(PeriodicTask* tasks, size_t count)
    : tasks_(tasks), count_(count) {}

void AppScheduler::begin(uint32_t nowMs) {
    for (size_t i = 0; i < count_; ++i) {
        tasks_[i].lastRunMs = nowMs;
        tasks_[i].missedDeadlines = 0;
    }
}

void AppScheduler::tick(uint32_t nowMs) {
    for (size_t i = 0; i < count_; ++i) {
        PeriodicTask& task = tasks_[i];
        if (task.run != nullptr && claimIfDue(task, nowMs)) {
            task.run();
        }
    }
}

bool AppScheduler::claimIfDue(PeriodicTask& task, uint32_t nowMs) {
    // TODO(human)
    (void)task;
    (void)nowMs;
    return false;
}

}  // namespace app
