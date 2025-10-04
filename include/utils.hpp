#pragma once
#include <iostream>
#include <stdexcept>

#define ERR_LIST E(GENERIC_EXCEPTION, "A generic exception has been thrown.")
namespace trm {

inline void clearConsole() { std::cout << "\033[2J\033[H" << std::flush; }

inline void showError(const std::string &errMsg) {
    clearConsole();
    std::cout << "\e[0;31m" << "THROWN EXCEPTION:\n"
              << errMsg << "\e[0m" << std::endl;
}

enum class Error {
    #define E(err, str) err,
    ERR_LIST
    #undef E
    COUNT
};

constexpr const char* errMsg[static_cast<std::size_t>(Error::COUNT)] = {
    #define E(err, str) str,
    ERR_LIST
    #undef E
};

inline void require(const bool cond, const Error err) {
    if (!cond) [[unlikely]] {
        throw std::runtime_error(errMsg[static_cast<std::size_t>(err)]);
    }
}

} // namespace trm
