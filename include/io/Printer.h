#pragma once
#include <ostream>

#include "Problem.h"

// Human-readable dump of the loaded input, to check it was read correctly.
void printProblem(std::ostream& out, const Problem& problem);
