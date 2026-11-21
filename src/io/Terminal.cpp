#include "io/Terminal.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN   // only the core of windows.h
#define NOMINMAX              // or windows.h defines min/max macros that break std::min
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004   // older Windows SDKs do not define it
#endif
#else
#include <unistd.h>
#endif

bool stdoutIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool enableAnsiColors() {
#ifdef _WIN32
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE || console == nullptr) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(console, &mode)) return false;   // not a console (a file or a pipe)
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

bool noColorRequested() {
#ifdef _WIN32
    // MSVC warns that getenv is unsafe; getenv_s is its safe version.
    std::size_t length = 0;
    getenv_s(&length, nullptr, 0, "NO_COLOR");
    return length > 1;   // length includes the terminating '\0'
#else
    const char* value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
#endif
}
