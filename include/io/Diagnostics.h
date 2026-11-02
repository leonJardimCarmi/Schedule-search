#pragma once
#include <ostream>
#include <string>
#include <vector>

// Problems found while loading input. Errors stop the program;
// warnings are printed but the run continues.
enum class Severity { Warning, Error };

struct Diagnostic {
    Severity severity;
    std::string where;     // e.g. "week.json: employees[1].availability[0].time"
    std::string message;
};

class Diagnostics {
public:
    void error(const std::string& where, const std::string& message);
    void warning(const std::string& where, const std::string& message);

    bool hasErrors() const;
    const std::vector<Diagnostic>& all() const { return items_; }

private:
    std::vector<Diagnostic> items_;
};

std::ostream& operator<<(std::ostream& out, const Diagnostic& d);
