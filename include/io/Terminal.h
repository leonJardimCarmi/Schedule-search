#pragma once

// The little the program needs to know about the console it writes to
// (step 6.3b, D42, D45). Everything that differs between Windows and the
// rest lives in Terminal.cpp, behind these two functions.

// True if standard output is a terminal, not a file or a pipe.
bool stdoutIsTerminal();

// Asks the console to understand ANSI color codes. On Windows the classic
// console needs to be told (ENABLE_VIRTUAL_TERMINAL_PROCESSING); elsewhere
// terminals understand them already. Returns false if it is not possible.
bool enableAnsiColors();

// True if the user asked for no colors with the NO_COLOR environment
// variable (a common convention, see no-color.org).
bool noColorRequested();
