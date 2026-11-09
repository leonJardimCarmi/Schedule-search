#include "io/JsonLoader.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

// Reads fields from JSON objects and records every problem with its location,
// instead of stopping at the first one. The user fixes all mistakes in one go.
class Reader {
public:
    Reader(std::string source, Diagnostics& diag) : source_(std::move(source)), diag_(diag) {}

    void error(const std::string& path, const std::string& msg) { diag_.error(at(path), msg); }
    void warning(const std::string& path, const std::string& msg) { diag_.warning(at(path), msg); }

    // A typo such as "minRestHour" would otherwise be silently ignored.
    void warnUnknownKeys(const json& obj, const std::string& path,
                         std::initializer_list<const char*> known) {
        for (const auto& item : obj.items()) {
            const bool isKnown = std::any_of(known.begin(), known.end(),
                [&](const char* k) { return item.key() == k; });
            if (!isKnown) warning(join(path, item.key()), "unknown field, ignored");
        }
    }

    std::optional<std::string> requiredString(const json& obj, const std::string& path, const char* key) {
        const json* v = find(obj, path, key, true);
        if (!v) return std::nullopt;
        if (!v->is_string()) { error(join(path, key), "expected a string"); return std::nullopt; }
        return v->get<std::string>();
    }

    std::optional<std::string> optionalString(const json& obj, const std::string& path, const char* key) {
        const json* v = find(obj, path, key, false);
        if (!v) return std::nullopt;
        if (!v->is_string()) { error(join(path, key), "expected a string"); return std::nullopt; }
        return v->get<std::string>();
    }

    std::optional<bool> optionalBool(const json& obj, const std::string& path, const char* key) {
        const json* v = find(obj, path, key, false);
        if (!v) return std::nullopt;
        if (!v->is_boolean()) { error(join(path, key), "expected true or false"); return std::nullopt; }
        return v->get<bool>();
    }

    std::optional<int> requiredInt(const json& obj, const std::string& path, const char* key) {
        const json* v = find(obj, path, key, true);
        return v ? toInt(*v, join(path, key)) : std::nullopt;
    }

    std::optional<int> optionalInt(const json& obj, const std::string& path, const char* key) {
        const json* v = find(obj, path, key, false);
        return v ? toInt(*v, join(path, key)) : std::nullopt;
    }

    // Wraps a parse function that throws (Date::fromString, TimePeriod::fromString)
    // and turns the exception into a diagnostic at the right path.
    template <typename T, typename Parse>
    std::optional<T> parse(const std::string& text, const std::string& path, Parse parseFn) {
        try {
            return parseFn(text);
        } catch (const std::exception& e) {
            error(path, e.what());
            return std::nullopt;
        }
    }

    static std::string join(const std::string& path, const std::string& key) {
        return path.empty() ? key : path + "." + key;
    }
    static std::string index(const std::string& path, std::size_t i) {
        return path + "[" + std::to_string(i) + "]";
    }

private:
    std::string at(const std::string& path) const { return source_ + ": " + path; }

    const json* find(const json& obj, const std::string& path, const char* key, bool required) {
        auto it = obj.find(key);
        if (it == obj.end()) {
            if (required) error(join(path, key), "missing required field");
            return nullptr;
        }
        return &*it;
    }

    std::optional<int> toInt(const json& v, const std::string& path) {
        if (!v.is_number_integer()) { error(path, "expected a whole number"); return std::nullopt; }
        return v.get<int>();
    }

    std::string source_;
    Diagnostics& diag_;
};

// ---- Pieces of the file ------------------------------------------------------

std::optional<AvailabilitySlot> readSlot(Reader& r, const json& j, const std::string& path) {
    if (!j.is_object()) { r.error(path, "expected an object"); return std::nullopt; }
    r.warnUnknownKeys(j, path, {"date", "time", "type", "double"});

    std::optional<Date> date;
    if (auto s = r.requiredString(j, path, "date"))
        date = r.parse<Date>(*s, Reader::join(path, "date"), Date::fromString);

    std::optional<TimePeriod> time;
    bool timeOk = true;
    if (auto s = r.optionalString(j, path, "time")) {
        time = r.parse<TimePeriod>(*s, Reader::join(path, "time"), TimePeriod::fromString);
        timeOk = time.has_value();
    }

    std::optional<AvailabilityType> type;
    if (auto s = r.requiredString(j, path, "type")) {
        type = parseAvailabilityType(*s);
        if (!type) r.error(Reader::join(path, "type"), "must be available, preferred or unavailable");
    }

    const bool allowsDouble = r.optionalBool(j, path, "double").value_or(false);
    if (allowsDouble && type == AvailabilityType::Unavailable)
        r.warning(Reader::join(path, "double"), "has no meaning on an unavailable slot, ignored");

    if (!date || !type || !timeOk) return std::nullopt;
    return AvailabilitySlot{*date, time, *type, allowsDouble && *type != AvailabilityType::Unavailable};
}

std::optional<Employee> readEmployee(Reader& r, const json& j, const std::string& path) {
    if (!j.is_object()) { r.error(path, "expected an object"); return std::nullopt; }
    r.warnUnknownKeys(j, path, {"id", "name", "maxShifts", "minShifts", "roles", "availability"});

    Employee e;
    auto id = r.requiredInt(j, path, "id");
    auto name = r.requiredString(j, path, "name");
    e.maxShifts = r.optionalInt(j, path, "maxShifts");
    e.minShifts = r.optionalInt(j, path, "minShifts");

    if (auto it = j.find("roles"); it != j.end()) {
        if (!it->is_array()) r.error(Reader::join(path, "roles"), "expected a list of strings");
        else
            for (std::size_t i = 0; i < it->size(); ++i) {
                const json& role = (*it)[i];
                if (role.is_string()) e.roles.push_back(role.get<std::string>());
                else r.error(Reader::index(Reader::join(path, "roles"), i), "expected a string");
            }
    }

    if (auto it = j.find("availability"); it != j.end()) {
        const std::string listPath = Reader::join(path, "availability");
        if (!it->is_array()) r.error(listPath, "expected a list");
        else
            for (std::size_t i = 0; i < it->size(); ++i)
                if (auto slot = readSlot(r, (*it)[i], Reader::index(listPath, i)))
                    e.availability.push_back(*slot);
    }

    if (!id || !name) return std::nullopt;
    e.id = *id;
    e.name = *name;
    return e;
}

std::optional<Shift> readShift(Reader& r, const json& j, const std::string& path) {
    if (!j.is_object()) { r.error(path, "expected an object"); return std::nullopt; }
    r.warnUnknownKeys(j, path, {"id", "date", "time", "min", "wanted", "role"});

    auto id = r.requiredString(j, path, "id");

    std::optional<Date> date;
    if (auto s = r.requiredString(j, path, "date"))
        date = r.parse<Date>(*s, Reader::join(path, "date"), Date::fromString);

    std::optional<TimePeriod> time;
    if (auto s = r.requiredString(j, path, "time"))
        time = r.parse<TimePeriod>(*s, Reader::join(path, "time"), TimePeriod::fromString);

    auto minStaff = r.requiredInt(j, path, "min");
    auto wanted = r.requiredInt(j, path, "wanted");
    auto role = r.optionalString(j, path, "role");

    if (!id || !date || !time || !minStaff || !wanted) return std::nullopt;
    return Shift{*id, *date, *time, *minStaff, *wanted, role.value_or("")};
}

Rules readRules(Reader& r, const json& root) {
    Rules rules;
    auto it = root.find("rules");
    if (it == root.end()) return rules;                // all defaults
    if (!it->is_object()) { r.error("rules", "expected an object"); return rules; }

    r.warnUnknownKeys(*it, "rules", {"minRestHours", "blockGapMinutes", "doubleAboveHours",
                                     "maxDoubleHours", "maxConsecutiveDays"});
    if (auto v = r.optionalInt(*it, "rules", "minRestHours")) rules.minRestHours = *v;
    if (auto v = r.optionalInt(*it, "rules", "blockGapMinutes")) rules.blockGapMinutes = *v;
    if (auto v = r.optionalInt(*it, "rules", "doubleAboveHours")) rules.doubleAboveHours = *v;
    if (auto v = r.optionalInt(*it, "rules", "maxDoubleHours")) rules.maxDoubleHours = *v;
    rules.maxConsecutiveDays = r.optionalInt(*it, "rules", "maxConsecutiveDays");
    return rules;
}

// ---- Checks that need the whole file -----------------------------------------

void validate(Reader& r, const Problem& p) {
    if (p.periodEnd < p.periodStart) r.error("period", "end is before start");

    std::set<int> employeeIds;
    std::set<std::string> allRoles;
    for (std::size_t i = 0; i < p.employees.size(); ++i) {
        const Employee& e = p.employees[i];
        const std::string path = Reader::index("employees", i);
        if (!employeeIds.insert(e.id).second)
            r.error(path, "duplicate employee id " + std::to_string(e.id));
        if (e.maxShifts && *e.maxShifts < 0) r.error(path + ".maxShifts", "must not be negative");
        if (e.minShifts && *e.minShifts < 0) r.error(path + ".minShifts", "must not be negative");
        if (e.minShifts && e.maxShifts && *e.minShifts > *e.maxShifts)
            r.error(path, "minShifts (" + std::to_string(*e.minShifts) + ") is larger than maxShifts (" +
                              std::to_string(*e.maxShifts) + ")");
        if (e.availability.empty()) r.warning(path, e.name + " has no availability and will never be assigned");
        for (const std::string& role : e.roles) allRoles.insert(role);

        for (std::size_t k = 0; k < e.availability.size(); ++k) {
            const Date d = e.availability[k].date;
            if (d < p.periodStart || d > p.periodEnd)
                r.warning(Reader::index(path + ".availability", k), d.toString() + " is outside the period, ignored");
        }
    }

    std::set<std::string> shiftIds;
    for (const Shift& s : p.shifts) {
        const std::string path = "shifts[" + s.id + "]";
        if (!shiftIds.insert(s.id).second) r.error(path, "duplicate shift id");
        if (s.minStaff < 0) r.error(path + ".min", "must not be negative");
        if (s.minStaff > s.wantedStaff) r.error(path, "min (" + std::to_string(s.minStaff) +
                                               ") is larger than wanted (" + std::to_string(s.wantedStaff) + ")");
        if (s.date < p.periodStart || s.date > p.periodEnd)
            r.error(path + ".date", s.date.toString() + " is outside the period");
        if (!s.role.empty() && allRoles.count(s.role) == 0)
            r.warning(path + ".role", "no employee has role '" + s.role + "'");
    }

    const Rules& rules = p.rules;
    if (rules.minRestHours < 0) r.error("rules.minRestHours", "must not be negative");
    if (rules.blockGapMinutes < 0) r.error("rules.blockGapMinutes", "must not be negative");
    if (rules.doubleAboveHours < 1) r.error("rules.doubleAboveHours", "must be at least 1");
    if (rules.maxDoubleHours < rules.doubleAboveHours)
        r.error("rules.maxDoubleHours", "must not be smaller than doubleAboveHours");
    if (rules.maxConsecutiveDays && *rules.maxConsecutiveDays < 1) r.error("rules.maxConsecutiveDays", "must be at least 1");
}

}  // namespace

// ---- Public API --------------------------------------------------------------

LoadResult JsonLoader::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        LoadResult result;
        result.diagnostics.error(path, "cannot open file");
        return result;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return loadString(buffer.str(), path);
}

LoadResult JsonLoader::loadString(const std::string& text, const std::string& sourceName) {
    LoadResult result;
    Reader r(sourceName, result.diagnostics);

    // 1. Syntax. nlohmann's message already includes line and column.
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        r.error("(syntax)", e.what());
        return result;
    }
    if (!root.is_object()) { r.error("(root)", "expected a JSON object"); return result; }
    r.warnUnknownKeys(root, "", {"period", "employees", "shifts", "rules"});

    // 2. Period. Without it nothing else can be checked, so it is fatal.
    std::optional<Date> start, end;
    if (auto it = root.find("period"); it == root.end() || !it->is_object()) {
        r.error("period", "missing or not an object");
    } else {
        if (auto s = r.requiredString(*it, "period", "start")) start = r.parse<Date>(*s, "period.start", Date::fromString);
        if (auto s = r.requiredString(*it, "period", "end")) end = r.parse<Date>(*s, "period.end", Date::fromString);
    }
    if (!start || !end) return result;

    Problem problem{*start, *end, {}, {}, {}};

    // 3. Lists. A broken item is reported and skipped; the rest still load.
    auto readList = [&](const char* key, auto readItem, auto& out) {
        auto it = root.find(key);
        if (it == root.end()) { r.error(key, "missing required list"); return; }
        if (!it->is_array()) { r.error(key, "expected a list"); return; }
        for (std::size_t i = 0; i < it->size(); ++i)
            if (auto item = readItem(r, (*it)[i], Reader::index(key, i)))
                out.push_back(std::move(*item));
    };
    readList("employees", readEmployee, problem.employees);
    readList("shifts", readShift, problem.shifts);
    problem.rules = readRules(r, root);

    // 4. Chronological order: the solver walks the shifts in this order.
    //    The id breaks ties so the order never depends on the input file order.
    std::sort(problem.shifts.begin(), problem.shifts.end(), [](const Shift& a, const Shift& b) {
        if (a.absoluteStart() != b.absoluteStart()) return a.absoluteStart() < b.absoluteStart();
        return a.id < b.id;
    });

    // 5. Cross-checks (duplicates, min > wanted, dates outside the period...).
    validate(r, problem);

    result.problem = std::move(problem);
    return result;
}
