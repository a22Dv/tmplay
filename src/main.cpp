#ifdef _WIN32
#include <Windows.h>
#endif

#include <exception>
#include <iostream>

#include "player.hpp"
#include "utils.hpp"

int main() {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::ios_base::sync_with_stdio(false);
    try {
        trm::Player pl{};
        pl.run();
    } catch (const std::exception &e) {
        trm::showError(e.what());
    }
}