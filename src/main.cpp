#include <exception>
#include <iostream>
#include <format>

#include "player.hpp"
#include "utils.hpp"

int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        tml::Player p{};
        p.run();
    } catch (const std::exception &e) {
        tml::clearConsole();
        std::cerr << std::format("UNHANDLED EXCEPTION: {}", e.what()) << std::endl;
    }
    return 0;
}