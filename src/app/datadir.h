#pragma once

// What boundd and bound-cli agree on: where the data directory is, and the RPC cookie in it.

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace bound {

inline std::optional<std::string> env(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return std::nullopt;
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

// %APPDATA%\BOUND\testnet on Windows, ~/.bound/testnet elsewhere.
inline std::filesystem::path default_datadir() {
#ifdef _WIN32
    if (const auto appdata = env("APPDATA")) return std::filesystem::path(*appdata) / "BOUND" / "testnet";
#else
    if (const auto home = env("HOME")) return std::filesystem::path(*home) / ".bound" / "testnet";
#endif
    return "bound-data";
}

inline std::filesystem::path cookie_path(const std::filesystem::path& datadir) { return datadir / ".cookie"; }

}  // namespace bound
