#pragma once
#include <optional>
#include <string>

#include "Problem.h"
#include "io/Diagnostics.h"

struct LoadResult {
    std::optional<Problem> problem;   // empty when loading failed
    Diagnostics diagnostics;          // every error and warning found

    bool ok() const { return problem.has_value() && !diagnostics.hasErrors(); }
};

class JsonLoader {
public:
    static LoadResult loadFile(const std::string& path);

    // Same as loadFile, but from text in memory. `sourceName` only appears
    // in messages. Used by the tests, so they need no files on disk.
    static LoadResult loadString(const std::string& text, const std::string& sourceName);
};
