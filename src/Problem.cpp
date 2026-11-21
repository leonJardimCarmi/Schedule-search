#include "Problem.h"

#include <algorithm>
#include <cctype>

std::optional<std::size_t> Problem::findEmployee(int id) const {
    for (std::size_t i = 0; i < employees.size(); ++i)
        if (employees[i].id == id) return i;
    return std::nullopt;
}

std::optional<std::size_t> Problem::findEmployee(const std::string& idOrName) const {
    const bool digits = !idOrName.empty() &&
                        std::all_of(idOrName.begin(), idOrName.end(), [](unsigned char c) { return std::isdigit(c); });
    if (digits && idOrName.size() < 10) return findEmployee(std::stoi(idOrName));

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    for (std::size_t i = 0; i < employees.size(); ++i)
        if (lower(employees[i].name) == lower(idOrName)) return i;
    return std::nullopt;
}

std::optional<std::size_t> Problem::findShift(const std::string& id) const {
    for (std::size_t i = 0; i < shifts.size(); ++i)
        if (shifts[i].id == id) return i;
    return std::nullopt;
}
