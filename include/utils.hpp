#pragma once

#include <filesystem>
#include <iostream>
#include <stdexcept>

#define ERR_LIST                                                                                                       \
    E(GENERIC, "A generic exception has been thrown.")                                                                 \
    E(MA_INIT, "Miniaudio initialization encountered an error.")                                                       \
    E(DOES_NOT_EXIST, "File does not exist.")                                                                          \
    E(ALLOC, "Memory allocation failure.")                                                                             \
    E(FFMPEG_OPEN, "File cannot be opened.")                                                                           \
    E(FFMPEG_FILTER, "Filter graph failure.")                                                                          \
    E(FFMPEG_DECODE, "File decode failure.")                                                                           \
    E(INVALID_COMMAND, "Invalid command.")                                                                             \
    E(INVALID_UTF8, "Invalid UTF-8 sequence.")

namespace trm {

inline void clearConsole() { std::cout << "\033[2J\033[H" << std::flush; }

inline void showError(const std::string &errMsg) {
    clearConsole();
    std::cout << "\e[0;31m" << "EXCEPTION:\n" << errMsg << "\e[0m" << std::endl;
}

enum class Error : std::uint8_t {
#define E(err, str) err,
    ERR_LIST COUNT,
#undef E
};

constexpr const char *errMsg[static_cast<std::size_t>(Error::COUNT)] = {
#define E(err, str) str,
    ERR_LIST
#undef E
};

inline std::string asU8(const std::filesystem::path &path) {
    std::u8string u8{path.u8string()};
    return std::string{reinterpret_cast<const char *>(u8.data()), u8.length()};
}

inline void require(const bool cond, const Error err) {
    if (!cond) [[unlikely]] {
        throw std::runtime_error(errMsg[static_cast<std::size_t>(err)]);
    }
}

} // namespace trm
