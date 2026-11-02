#include "io/Diagnostics.h"

void Diagnostics::error(const std::string& where, const std::string& message) {
    items_.push_back({Severity::Error, where, message});
}

void Diagnostics::warning(const std::string& where, const std::string& message) {
    items_.push_back({Severity::Warning, where, message});
}

bool Diagnostics::hasErrors() const {
    for (const Diagnostic& d : items_)
        if (d.severity == Severity::Error) return true;
    return false;
}

std::ostream& operator<<(std::ostream& out, const Diagnostic& d) {
    return out << (d.severity == Severity::Error ? "error: " : "warning: ")
               << d.where << ": " << d.message;
}
