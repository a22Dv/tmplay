#include <exception>
#include <iostream>

#include "utils.hpp"

int main() {
    std::ios_base::sync_with_stdio(false);
    try {
        std::cout << "Hello World!\n";
    } catch (const std::exception &e) {
        trm::showError(e.what());
    }
}