#include "player.hpp"
#include "utils.hpp"
#include <filesystem>
#include <iostream>
#include <utility>

int main() {
    tml::Player p{};
    SetConsoleOutputCP(CP_UTF8);
    for (const std::pair<unsigned int, tml::AudioEntry> &entry : p.getEntries()) {
        std::cout << reinterpret_cast<const char*>(entry.second.filePath.u8string().data()) << std::endl;
    }
    return 0;
}