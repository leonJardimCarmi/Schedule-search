#pragma once
#include <string>

// Time range inside a single day, in minutes since 00:00.
// If end <= start the period crosses midnight, and end is stored as end + 1440.
class TimePeriod {
private:
	int start;
	int end;

	static int parseTimeToMinutes(const std::string& time_str); // "HH:MM" -> minutes

public:
	static const int MINUTES_PER_DAY = 1440;

	TimePeriod(int start_min, int end_min);
	static TimePeriod fromString(const std::string& range);     // "HH:MM-HH:MM"

	int getStart() const { return start; }
	int getEnd() const { return end; }
	int duration() const { return end - start; }

	bool contains(const TimePeriod& other) const;
	bool overlaps(const TimePeriod& other) const;
	std::string toString() const;
};