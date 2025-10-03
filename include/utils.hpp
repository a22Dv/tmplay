#pragma once
#include <iostream>

namespace trm {

inline void clearConsole() { std::cout << "\033[2J\033[H" << std::flush; }

inline void showError(const std::string &errMsg) {
    clearConsole();
    std::cout << "\e[0;31m" << "THROWN EXCEPTION:\n"
              << errMsg << "\e[0m" << std::endl;
}

} // namespace trm
