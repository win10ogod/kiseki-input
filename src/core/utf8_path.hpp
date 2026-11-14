#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kiseki::core {

// UTF-8 text to a path on every platform. Replaces std::filesystem::u8path,
// which C++20 deprecates: char8_t input is decoded as UTF-8 (UTF-16 on Windows,
// unchanged bytes on POSIX).
inline std::filesystem::path path_from_utf8(std::string_view text) {
#if defined(__cpp_char8_t) && defined(__cpp_lib_char8_t)
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const char c : text) {
        utf8.push_back(static_cast<char8_t>(c));
    }
    return std::filesystem::path{utf8};
#else
    return std::filesystem::u8path(text.begin(), text.end());
#endif
}

// A path as UTF-8 text on every platform.
inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

}
