#include "ScheduleContext.h"

void ScheduleContext::addWorkDay(int person_id, int day_idx) {
    worker_days[person_id].insert(day_idx);
}

void ScheduleContext::removeWorkDay(int person_id, int day_idx) {
    auto it = worker_days.find(person_id);
    if (it != worker_days.end()) it->second.erase(day_idx);
}

