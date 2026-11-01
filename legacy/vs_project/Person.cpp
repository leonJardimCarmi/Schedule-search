#include "Person.h"

void Person::addAvailability(int day_idx, const string& time_range) {
	availability_slots[day_idx].push_back(TimePeriod::fromString(time_range));
	days_count++;
}

bool Person::isAvailable(int day_idx, const TimePeriod& period) const {
	auto it = availability_slots.find(day_idx);
	if (it == availability_slots.end()) {
		return false;
	}

	for (const TimePeriod& slot : it->second) {
		if (slot.contains(period)) {
			return true;
		}
	}
	return false;
}