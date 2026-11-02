#pragma once
#include <string>

// A calendar date (no time of day), stored as the number of days since
// 1970-01-01. Storing a single integer makes the operations the solver needs
// trivial: comparing dates, "how many days apart", "the next day".
class Date {
public:
    // Parses "YYYY-MM-DD" (ISO 8601). Throws std::invalid_argument on bad
    // format or on a date that does not exist, such as 2026-02-30.
    static Date fromString(const std::string& text);
    static Date fromDays(int daysSinceEpoch) { return Date(daysSinceEpoch); }

    int daysSinceEpoch() const { return days_; }
    int weekday() const;                 // 0 = Sunday ... 6 = Saturday
    std::string toString() const;        // "YYYY-MM-DD"

    Date operator+(int days) const { return Date(days_ + days); }
    int operator-(const Date& other) const { return days_ - other.days_; }

    bool operator==(const Date& o) const { return days_ == o.days_; }
    bool operator!=(const Date& o) const { return days_ != o.days_; }
    bool operator<(const Date& o) const { return days_ < o.days_; }
    bool operator<=(const Date& o) const { return days_ <= o.days_; }
    bool operator>(const Date& o) const { return days_ > o.days_; }
    bool operator>=(const Date& o) const { return days_ >= o.days_; }

private:
    explicit Date(int days) : days_(days) {}
    int days_;
};
