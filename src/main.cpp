#ifndef NOCATCH
#include <exception>
#include <format>
#include <iostream>
#endif

#include "player.hpp"

#ifndef NOCATCH
#include "utils.hpp"
#endif


int main() {
    SetConsoleOutputCP(CP_UTF8);
#ifndef NOCATCH
    try {
#endif
        tml::Player p{};
        p.run();
#ifndef NOCATCH
    } catch (const std::exception &e) {
        tml::clearConsole();
        std::cerr << std::format("UNHANDLED EXCEPTION: {}", e.what()) << std::endl;
    }
#endif
    return 0;
}