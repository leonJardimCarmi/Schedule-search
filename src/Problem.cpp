#include "Problem.h"

std::optional<std::size_t> Problem::findEmployee(int id) const {
    for (std::size_t i = 0; i < employees.size(); ++i)
        if (employees[i].id == id) return i;
    return std::nullopt;
}
