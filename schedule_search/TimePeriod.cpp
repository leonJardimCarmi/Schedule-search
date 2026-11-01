#include "TimePeriod.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>

TimePeriod::TimePeriod(int start_min, int end_min) : start(start_min), end(end_min) {
	if (end <= start) end += MINUTES_PER_DAY; // overnight, e.g. 22:00-06:00
}

int TimePeriod::parseTimeToMinutes(const std::string& time_str) {
	size_t colon_pos = time_str.find(':');
	if (colon_pos == std::string::npos)
		throw std::invalid_argument("bad time: " + time_str);

	int hours = std::stoi(time_str.substr(0, colon_pos));
	int minutes = std::stoi(time_str.substr(colon_pos + 1));

	if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
		throw std::invalid_argument("bad time: " + time_str);

	return hours * 60 + minutes;
}

TimePeriod TimePeriod::fromString(const std::string& range) {
	size_t dash_pos = range.find('-');
	if (dash_pos == std::string::npos)
		throw std::invalid_argument("bad time range: " + range);

	return TimePeriod(parseTimeToMinutes(range.substr(0, dash_pos)),
		parseTimeToMinutes(range.substr(dash_pos + 1)));
}

bool TimePeriod::contains(const TimePeriod& other) const {
	return start <= other.start && other.end <= end;
}

bool TimePeriod::overlaps(const TimePeriod& other) const {
	return start < other.end && other.start < end;
}

std::string TimePeriod::toString() const {
	std::ostringstream out;
	out << std::setfill('0')
		<< std::setw(2) << (start / 60) % 24 << ':' << std::setw(2) << start % 60 << '-'
		<< std::setw(2) << (end / 60) % 24 << ':' << std::setw(2) << end % 60;
	return out.str();
}