#pragma once

// Layer log: one file per run in ETERNALVR_LOG_DIR, every line flushed so a crash keeps the tail.
// Without ETERNALVR_LOG_DIR the log goes to OutputDebugString only.

#include <string>

namespace evr::vkcore {

// Opens the log file (once) and writes the loaded marker. Safe to call more than once.
void logOpen();

void logf(const char* format, ...);

// Seconds since the log started: the time in front of every log line.
double logSeconds();

// The value of ETERNALVR_LOG_DIR, empty when unset.
std::wstring logDirectory();

// Logs the file version of the vulkan-1.dll the game loaded (T-079).
void logLoaderVersion();

// Reads an environment variable; false when it is not set.
bool readEnv(const wchar_t* name, std::wstring& value);

} // namespace evr::vkcore

#define EVR_LOG(...) ::evr::vkcore::logf(__VA_ARGS__)
