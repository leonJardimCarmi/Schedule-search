#include "Date.h"

#include <cstdio>
#include <stdexcept>

namespace {

struct YearMonthDay {
    int year;
    unsigned month;   // 1..12
    unsigned day;     // 1..31
};

// Days since 1970-01-01 for a proleptic Gregorian date.
// Algorithm by Howard Hinnant ("chrono-compatible low-level date algorithms").
int daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);                 // [0, 399]
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;    // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;               // [0, 146096]
    return era * 146097 + static_cast<int>(doe) - 719468;
}

// Inverse of daysFromCivil.
YearMonthDay civilFromDays(int z) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int y = static_cast<int>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    return {y, m, d};
}

bool isDigits(const std::string& s, std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

}  // namespace

Date Date::fromString(const std::string& text) {
    // Exact shape check first: "YYYY-MM-DD" is 10 characters.
    if (text.size() != 10 || text[4] != '-' || text[7] != '-' ||
        !isDigits(text, 0, 4) || !isDigits(text, 5, 2) || !isDigits(text, 8, 2))
        throw std::invalid_argument("bad date (expected YYYY-MM-DD): " + text);

    const int y = std::stoi(text.substr(0, 4));
    const int m = std::stoi(text.substr(5, 2));
    const int d = std::stoi(text.substr(8, 2));
    if (m < 1 || m > 12 || d < 1 || d > 31)
        throw std::invalid_argument("bad date: " + text);

    // Round-trip check: 2026-02-30 would silently become 2026-03-02.
    // If converting back does not give the same y/m/d, the date does not exist.
    const int days = daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
    const YearMonthDay back = civilFromDays(days);
    if (back.year != y || back.month != static_cast<unsigned>(m) || back.day != static_cast<unsigned>(d))
        throw std::invalid_argument("date does not exist: " + text);

    return Date(days);
}

int Date::weekday() const {
    // 1970-01-01 was a Thursday (4). The extra "+ 7) % 7" keeps it
    // non-negative for dates before 1970.
    return ((days_ + 4) % 7 + 7) % 7;
}

std::string Date::toString() const {
    const YearMonthDay ymd = civilFromDays(days_);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", ymd.year, ymd.month, ymd.day);
    return buf;
}
